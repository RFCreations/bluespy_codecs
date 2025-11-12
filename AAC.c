// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"
#include "codec_structures.h"

#include "aacdecoder_lib.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init()
{
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name  = "AAC"
    };
}

#define MAX_STREAMS 16

typedef struct AAC_handle {
    HANDLE_AACDECODER aac;
    bool initialized;
    uint32_t sample_rate;
    uint8_t  n_channels;
    bluespy_audiostream_id stream_id;
    uint64_t last_sequence_number;
    int in_use;
    int16_t pcm_buf[32768];
} AAC_handle_t;

static AAC_handle_t handles[MAX_STREAMS] = {0};

static AAC_handle_t* get_handle(bluespy_audiostream_id id)
{
    for (int i = 0; i < MAX_STREAMS; ++i)
        if (handles[i].in_use && handles[i].stream_id == id)
            return &handles[i];

    for (int i = 0; i < MAX_STREAMS; ++i)
        if (!handles[i].in_use) {
            handles[i].in_use          = 1;
            handles[i].stream_id       = id;
            handles[i].initialized     = false;
            handles[i].aac             = NULL;
            handles[i].last_sequence_number = (uint64_t)-1;
            memset(handles[i].pcm_buf, 0, sizeof(handles[i].pcm_buf));
            return &handles[i];
        }

    return NULL;
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret r = {
        .error = -1,
        .format = {0, 0, 0},
        .fns = {NULL, NULL}
    };

    const AVDTP_Service_Capabilities_Media_Codec_t* cap = (const AVDTP_Service_Capabilities_Media_Codec_t*)info->config;

    if (cap->Media_Codec_Type != AVDTP_Codec_MPEG_24_AAC)
        return r;

    if (info->config_len < 6) {
        r.error = -2;
        return r;
    }

    AAC_handle_t* handle = get_handle(id);
    if (!handle) {
        r.error = -10;
        return r;
    }

    /* Parse codec‑specific info (see A2DP AAC spec section 4.5.2.6.) */
    const uint8_t* cfg = cap->Media_Codec_Specific_Information;
    uint8_t sample_bitfield = cfg[1];
    uint8_t chan_bitfield   = cfg[2];

    if (sample_bitfield & 0x80)      r.format.sample_rate = 8000;
    else if (sample_bitfield & 0x40) r.format.sample_rate = 11025;
    else if (sample_bitfield & 0x20) r.format.sample_rate = 12000;
    else if (sample_bitfield & 0x10) r.format.sample_rate = 16000;
    else if (sample_bitfield & 0x08) r.format.sample_rate = 22050;
    else if (sample_bitfield & 0x04) r.format.sample_rate = 24000;
    else if (sample_bitfield & 0x02) r.format.sample_rate = 32000;
    else if (sample_bitfield & 0x01) r.format.sample_rate = 44100;
    else if (chan_bitfield & 0x80)   r.format.sample_rate = 48000;
    else if (chan_bitfield & 0x40)   r.format.sample_rate = 64000;
    else if (chan_bitfield & 0x20)   r.format.sample_rate = 88200;
    else if (chan_bitfield & 0x10)   r.format.sample_rate = 96000;
    else {
        r.error = -3;
        return r;
    }

    /* Channels */
    r.format.n_channels =
        (chan_bitfield & 0x08) ? 1 :
        (chan_bitfield & 0x04) ? 2 : 2;

    r.format.bits_per_sample = 16;

    /* Init decoder */
    handle->aac = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);
    if (!handle->aac) {
        r.error = -5;
        return r;
    }

    if (aacDecoder_SetParam(handle->aac, AAC_PCM_MIN_OUTPUT_CHANNELS, r.format.n_channels) != AAC_DEC_OK ||
        aacDecoder_SetParam(handle->aac, AAC_PCM_MAX_OUTPUT_CHANNELS, r.format.n_channels) != AAC_DEC_OK) {
        aacDecoder_Close(handle->aac);
        handle->aac = NULL;
        r.error = -6;
        return r;
    }

    handle->sample_rate = r.format.sample_rate;
    handle->n_channels  = r.format.n_channels;
    handle->initialized = true;
    handle->last_sequence_number = (uint64_t)-1;

    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;

    return r;
}

BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id)
{
    AAC_handle_t* handle = get_handle(id);
    if (handle) {
        if (handle->aac)
            aacDecoder_Close(handle->aac);
        handle->aac = NULL;
        handle->in_use = 0;
        handle->initialized = false;
        handle->last_sequence_number = (uint64_t)-1;
    }
}

BLUESPY_CODEC_API void codec_decode(bluespy_audiostream_id id,
                                    const uint8_t* payload,
                                    const uint32_t payload_len,
                                    bluespy_event_id event_id,
                                    uint64_t sequence_number)
{
    AAC_handle_t* handle = get_handle(id);
    if (!handle || !handle->initialized || !handle->aac || !payload)
        return;

    if (payload_len < 12)
        return;

    /* Strip RTP header if present */
    uint32_t csrc_count = payload[0] & 0x0F;
    uint32_t rtp_hdr_len = 12 + 4 * csrc_count;
    if (payload_len <= rtp_hdr_len)
        return;
    payload += rtp_hdr_len;
    uint32_t payload_left = payload_len - rtp_hdr_len;

    UINT flags = 0;
    if (handle->last_sequence_number != (uint64_t)-1 && sequence_number != handle->last_sequence_number + 1)
        flags |= AACDEC_INTR;
    handle->last_sequence_number = sequence_number;

    int16_t* pcm_buf = handle->pcm_buf;
    size_t total_bytes_written = 0;

    while (payload_left > 0) {
        UINT valid = payload_left;
        const UCHAR* in_ptr = payload;

        if (aacDecoder_Fill(handle->aac, (UCHAR**)&in_ptr, &payload_left, &valid) != AAC_DEC_OK)
            break;

        CStreamInfo* info = aacDecoder_GetStreamInfo(handle->aac);
        if (!info)
            break;

        UINT frame_buffer_size = sizeof(handle->pcm_buf) - total_bytes_written;
        short* out_pos = pcm_buf + (total_bytes_written / sizeof(short));

        AAC_DECODER_ERROR dec_err = aacDecoder_DecodeFrame(handle->aac, out_pos, frame_buffer_size / sizeof(short), flags);
        if (dec_err != AAC_DEC_OK)
            break;

        UINT frame_samples = info->frameSize * info->numChannels;
        UINT frame_bytes   = frame_samples * sizeof(short);

        total_bytes_written += frame_bytes;
        if (total_bytes_written + frame_bytes > sizeof(handle->pcm_buf))
            break;

        payload_left = valid;
    }

    if (total_bytes_written == 0)
        return;

    bluespy_add_decoded_audio((const uint8_t*)pcm_buf, (uint32_t)total_bytes_written, event_id);
}

#ifdef __cplusplus
}
#endif