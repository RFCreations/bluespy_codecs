// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"
#include "codec_structures.h"
#include <lc3.h>

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ------------------- Constants ------------------- */

#define MAX_STREAMS  16
#define MAX_CHANNELS 8

/* ------------------------------------------------------------------------- */
/*  Per‑stream handle                                                        */
/* ------------------------------------------------------------------------- */

typedef struct {
    bluespy_audiostream_id stream_id;
    int                   in_use;

    uint8_t  channels;
    uint32_t sample_rate_hz;
    uint32_t frame_duration_us;
    uint16_t bytes_per_channel;

    void*          decoder_mem[MAX_CHANNELS];
    lc3_decoder_t  decoder[MAX_CHANNELS];
    int16_t*       pcm_buffer;
    size_t         samples_per_frame;
} LC3Handle;

static LC3Handle handles[MAX_STREAMS];

/* ------------------------------------------------------------------------- */
/*  Helpers                                                                  */
/* ------------------------------------------------------------------------- */

static LC3Handle* find_or_allocate_handle(bluespy_audiostream_id id)
{
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (handles[i].in_use && handles[i].stream_id == id)
            return &handles[i];

    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!handles[i].in_use) {
            memset(&handles[i], 0, sizeof(handles[i]));
            handles[i].in_use    = 1;
            handles[i].stream_id = id;
            return &handles[i];
        }
    }
    return NULL;
}

/* Translate Bluetooth Sampling Frequency bit‑mask to rate in Hz. */
static uint32_t decode_sampling_freq(uint16_t mask)
{
    if (mask & 0x0020) return 48000;
    if (mask & 0x0010) return 44100;
    if (mask & 0x0008) return 32000;
    if (mask & 0x0004) return 24000;
    if (mask & 0x0002) return 16000;
    if (mask & 0x0001) return 8000;
    return 48000;
}

/* Count set bits in the channel map. */
static uint8_t count_bits(const uint8_t* data, uint8_t length)
{
    uint32_t mask = 0;
    for (uint8_t i = 0; i < length; i++)
        mask |= ((uint32_t)data[i]) << (i * 8);

#if defined(__GNUC__)
    return (uint8_t)__builtin_popcount(mask);
#else
    uint8_t count = 0;
    while (mask) { count += mask & 1; mask >>= 1; }
    return count;
#endif
}

/* ------------------------------------------------------------------------- */
/*  TLV parsing                                                              */
/* ------------------------------------------------------------------------- */

static void parse_lea_configuration(LC3Handle* handle,
                                    const uint8_t* tlv_ptr,
                                    uint32_t total_length)
{
    const uint8_t* end = tlv_ptr + total_length;

    /* defaults */
    handle->sample_rate_hz    = 48000;
    handle->frame_duration_us = 10000;
    handle->channels          = 1;
    handle->bytes_per_channel = 60;

    while (tlv_ptr + 2 <= end) {
        uint8_t length = tlv_ptr[0];
        uint8_t type   = tlv_ptr[1];
        if (length == 0) { 
            ++tlv_ptr; 
            continue;
        }
        if (tlv_ptr + length > end) 
            break;

        const uint8_t* value = tlv_ptr + 2;
        uint8_t vlen = length - 1;

        switch (type) {
        case 0x01: { /* Sampling Frequency */
            uint16_t val = 0;
            for (uint8_t i = 0; i < vlen; ++i)
                val |= (uint16_t)value[i] << (8 * i);
            handle->sample_rate_hz = decode_sampling_freq(val);
            break;
        }
        case 0x02: /* Frame Duration */
            if (vlen >= 1)
                handle->frame_duration_us = (value[0] == 0x00) ? 7500 : 10000;
            break;

        case 0x03: /* Audio Channel Allocation */
            handle->channels = count_bits(value, vlen);
            if (handle->channels == 0) 
                handle->channels = 1;
            break;

        case 0x04: /* Octets per Frame */
            handle->bytes_per_channel = (vlen >= 2) ? (value[0] | (value[1] << 8)) : value[0];
            break;

        default:
            break;
        }

        tlv_ptr += length;
    }
}

/* ------------------------------------------------------------------------- */
/*  blueSPY entry‑points                                                     */
/* ------------------------------------------------------------------------- */

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void)
{
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name  = "LC3"
    };
}

/* ------------------------------------------------------------------------- */
/*  new_codec_stream()                                                      */
/* ------------------------------------------------------------------------- */
BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret ret = { .error = -1 };

    if (!info || !info->config || info->config_len < sizeof(LEA_Codec_Specific_Config_t))
        return ret;
    if (info->container != BLUESPY_CODEC_LEA && info->container != BLUESPY_CODEC_LEA_BIS)
        return ret;

    LC3Handle* handle = find_or_allocate_handle(id);
    if (!handle) 
        return ret;

    const uint8_t* cfg_bytes = (const uint8_t*)info->config;
    const LEA_Codec_Specific_Config_t* cfg = (const LEA_Codec_Specific_Config_t*)cfg_bytes;

    /* Base right after the 4‑byte LC3 header */
    const uint8_t* base = cfg_bytes + 4;
    const uint8_t* tlv_start = base;

    size_t avail_bytes = info->config_len - (tlv_start - cfg_bytes);
    parse_lea_configuration(handle, tlv_start, (uint32_t)avail_bytes);

    if (handle->channels > MAX_CHANNELS)
        handle->channels = MAX_CHANNELS;

    unsigned dec_size = lc3_decoder_size(handle->frame_duration_us, handle->sample_rate_hz);
    if (!dec_size) { 
        ret.error = -2; 
        return ret;
    }

    handle->samples_per_frame = lc3_frame_samples(handle->frame_duration_us, handle->sample_rate_hz);
    size_t pcm_bytes = handle->samples_per_frame * handle->channels * sizeof(int16_t);
    handle->pcm_buffer = (int16_t*)calloc(1, pcm_bytes);
    if (!handle->pcm_buffer) { 
        ret.error = -3;
        return ret;
    }

    for (uint8_t c = 0; c < handle->channels; ++c) {
        handle->decoder_mem[c] = calloc(1, dec_size);
        handle->decoder[c] = lc3_setup_decoder(handle->frame_duration_us,
                                               handle->sample_rate_hz,
                                               0, handle->decoder_mem[c]);
        if (!handle->decoder[c]) { 
            ret.error = -5; 
            return ret; 
        }
    }

    ret.error = 0;
    ret.format.sample_rate     = handle->sample_rate_hz;
    ret.format.n_channels      = handle->channels;
    ret.format.bits_per_sample = 16;
    ret.fns.decode             = codec_decode;
    ret.fns.deinit             = codec_deinit;
    return ret;
}

/* ------------------------------------------------------------------------- */
/*  codec_deinit()                                                          */
/* ------------------------------------------------------------------------- */
BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id)
{
    LC3Handle* handle = find_or_allocate_handle(id);
    if (!handle) 
        return;

    for (uint8_t c = 0; c < MAX_CHANNELS; ++c) {
        free(handle->decoder_mem[c]);
        handle->decoder_mem[c] = NULL;
        handle->decoder[c]     = NULL;
    }
    free(handle->pcm_buffer);
    memset(handle, 0, sizeof(*handle));
}

/* ------------------------------------------------------------------------- */
/*  codec_decode()                                                          */
/* ------------------------------------------------------------------------- */
BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id,
                                                                 const uint8_t* payload,
                                                                 const uint32_t payload_len,
                                                                 bluespy_event_id event_id)
{
    bluespy_audio_codec_decoded_audio out = { .data=NULL, .len=0, .source_id=event_id };
    LC3Handle* handle = find_or_allocate_handle(id);
    if (!handle || !handle->channels || !payload || payload_len==0) 
        return out;

    const size_t samples = handle->samples_per_frame;
    const uint32_t bytes_per_ch = handle->bytes_per_channel;
    const int stride = handle->channels;
    int16_t* pcm = handle->pcm_buffer;
    memset(pcm, 0, samples * stride * sizeof(int16_t));

    for (uint8_t c=0; c<handle->channels; ++c) {
        const uint8_t* frame = payload + c * bytes_per_ch;
        uint32_t frame_len = (payload_len < bytes_per_ch) ? payload_len : bytes_per_ch;
        lc3_decode(handle->decoder[c], frame, frame_len,
                   LC3_PCM_FORMAT_S16, pcm + c, stride);
    }

    out.data = (uint8_t*)pcm;
    out.len  = samples * stride * sizeof(int16_t);
    return out;
}