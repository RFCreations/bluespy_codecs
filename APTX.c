// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"
#include "codec_structures.h"
#include "freeaptx.h"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void)
{
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name  = "aptX"
    };
}

#define MAX_STREAMS 16

typedef struct APTX_handle {
    struct aptx_context* aptx;
    bool      hd;
    uint32_t  sample_rate;
    uint8_t   n_channels;
    uint32_t  last_timestamp;
    uint32_t  total_samples;
    bluespy_audiostream_id stream_id;
    bool      initialized;
    bool      in_use;
    uint8_t   out_buf[32768];       /* raw 24‑bit PCM from freeaptx */
    int16_t   final_output[32768];  /* converted 16‑bit PCM */
} APTX_handle_t;

static APTX_handle_t handles[MAX_STREAMS] = {0};

static APTX_handle_t* get_handle(bluespy_audiostream_id id)
{
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (handles[i].in_use && handles[i].stream_id == id)
            return &handles[i];

    for (int i = 0; i < MAX_STREAMS; ++i)
        if (!handles[i].in_use) {
            memset(&handles[i], 0, sizeof(APTX_handle_t));
            handles[i].in_use     = true;
            handles[i].stream_id  = id;
            handles[i].initialized = false;
            return &handles[i];
        }

    return NULL;
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret r = {.error = -1, .format = {0,0,0}, .fns = {NULL,NULL}};

    const AVDTP_Service_Capabilities_Media_Codec_t* cap = (const AVDTP_Service_Capabilities_Media_Codec_t*)info->config;

    if (!cap || cap->Media_Codec_Type != AVDTP_Codec_Vendor_Specific)
        return r;

    uint32_t vendor_id =
        ((uint32_t)cap->Media_Codec_Specific_Information[3] << 24) |
        ((uint32_t)cap->Media_Codec_Specific_Information[2] << 16) |
        ((uint32_t)cap->Media_Codec_Specific_Information[1] << 8)  |
         (uint32_t)cap->Media_Codec_Specific_Information[0];
    uint8_t vendor_codec_id = cap->Media_Codec_Specific_Information[4];

    if (vendor_id != 0x0000004F ||
        (vendor_codec_id != 0x01 && vendor_codec_id != 0x02))
        return r;

    APTX_handle_t* handle = get_handle(id);
    if (!handle) {
        r.error = -10;
        return r;
    }

    handle->hd = (vendor_codec_id == 0x02);

    /* aptX is always stereo */
    handle->n_channels = 2;
    r.format.n_channels = 2;
    r.format.bits_per_sample = 16;

    /* Parse sample rate (bit 3 = 48 kHz, bit 4 = 44.1 kHz) */
    uint8_t cap_byte = cap->Media_Codec_Specific_Information[5];
    if (cap_byte & 0x08)
        r.format.sample_rate = 48000;
    else if (cap_byte & 0x10)
        r.format.sample_rate = 44100;
    else
        r.format.sample_rate = 48000;

    handle->sample_rate = r.format.sample_rate;
    handle->last_timestamp = 0;
    handle->total_samples  = 0;

    if (handle->aptx)
        aptx_finish(handle->aptx);

    handle->aptx = aptx_init(handle->hd);
    if (!handle->aptx) {
        handle->in_use = false;
        r.error = -5;
        return r;
    }

    handle->initialized = true;

    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;
    return r;
}

BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id)
{
    APTX_handle_t* handle = get_handle(id);
    if (!handle)
        return;

    if (handle->aptx) {
        aptx_finish(handle->aptx);
        handle->aptx = NULL;
    }

    handle->in_use = false;
    handle->initialized = false;
}

BLUESPY_CODEC_API void codec_decode(bluespy_audiostream_id id,
                                    const uint8_t* payload,
                                    uint32_t payload_len,
                                    bluespy_event_id event_id,
                                    uint64_t sequence_number)
{
    (void)sequence_number;

    APTX_handle_t* handle = get_handle(id);
    if (!handle || !handle->aptx || !handle->initialized ||
        !payload || payload_len == 0)
        return;

    /* aptX packets are typically raw codec frames without extra headers */
    const uint8_t* coded_data = payload;
    size_t coded_len = payload_len;

    /* Decode to 24‑bit PCM */
    size_t written_bytes = 0;
    (void)aptx_decode(handle->aptx,
                      coded_data,
                      coded_len,
                      handle->out_buf,
                      sizeof(handle->out_buf),
                      &written_bytes);

    if (written_bytes == 0)
        return;

    /* Convert 24‑bit little‑endian PCM → 16‑bit signed PCM */
    size_t final_samples = 0;
    const size_t sample_cap = sizeof(handle->final_output) / sizeof(handle->final_output[0]);

    for (size_t i = 0;
         i + 2 < written_bytes && final_samples < sample_cap;
         i += 3)
    {
        int32_t s = (int32_t)handle->out_buf[i] |
                    ((int32_t)handle->out_buf[i + 1] << 8) |
                    ((int32_t)handle->out_buf[i + 2] << 16);
        if (s & 0x00800000)
            s |= 0xFF000000;  /* sign‑extend 24 → 32 bit */
        handle->final_output[final_samples++] = (int16_t)(s >> 8);
    }

    if (final_samples == 0)
        return;

    uint32_t bytes_out = (uint32_t)(final_samples * sizeof(int16_t));

    bluespy_add_decoded_audio((const uint8_t*)handle->final_output, bytes_out, event_id);

    handle->total_samples += final_samples / handle->n_channels;
}

#ifdef __cplusplus
}
#endif