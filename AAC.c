// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

// #include "bluespy.h"
// #include "bluespy_codec_defs.h"
#include "bluespy_codec_interface.h"
#include "aacdecoder_lib.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus 
extern "C" { 
#endif

bluespy_audio_codec_lib_info init() { 
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION, 
        .codec_name = "AAC"
    }; 
}

// Support multiple concurrent streams
#define MAX_STREAMS 16

static struct AAC_handle {
    HANDLE_AACDECODER aac;
    uint32_t sequence_number;
    bluespy_audiostream_id stream_id;
    int in_use;
    uint8_t out_buf[32768];
} handles[MAX_STREAMS] = {0};

// Find or allocate a handle for a stream
static struct AAC_handle* get_handle(bluespy_audiostream_id id) {
    // First, check if we already have a handle for this ID
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (handles[i].in_use && handles[i].stream_id == id) {
            return &handles[i];
        }
    }
    
    // Allocate a new handle
    for (int i = 0; i < MAX_STREAMS; i++) {
        if (!handles[i].in_use) {
            handles[i].in_use = 1;
            handles[i].stream_id = id;
            handles[i].aac = NULL;
            handles[i].sequence_number = -1;
            return &handles[i];
        }
    }
    
    return NULL; // No free slots
}

bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret r = {.error = -1, .format = {0, 0, 0}, .fns = {NULL, NULL}};
    
    const AVDTP_Service_Capabilities_Media_Codec_t* cap = (const AVDTP_Service_Capabilities_Media_Codec_t*) info->config;
    
    if(cap->Media_Codec_Type != AVDTP_Codec_MPEG_24_AAC)
        return r;
    
    if(info->config_len < 6) {
        r.error = -2;    
        return r;
    }

    struct AAC_handle* handle = get_handle(id);
    if (!handle) {
        r.error = -10; // Too many concurrent streams
        return r;
    }

    struct {
        uint8_t object_type;
        uint8_t sample_rate_ls;
        uint8_t chan_sample_rate;
        uint8_t bit_rate_ls_vbr;
        uint8_t bit_rate_mid;
        uint8_t bit_rate_ms;
    } codec_data;

    memcpy(&codec_data, cap->Media_Codec_Specific_Information, 6);

    if (codec_data.sample_rate_ls >> 7 & 1) {
        r.format.sample_rate = 8000;
    } else if (codec_data.sample_rate_ls >> 6 & 1) {
        r.format.sample_rate = 11025;
    } else if (codec_data.sample_rate_ls >> 5 & 1) {
        r.format.sample_rate = 12000;
    } else if (codec_data.sample_rate_ls >> 4 & 1) {
        r.format.sample_rate = 16000;
    } else if (codec_data.sample_rate_ls >> 3 & 1) {
        r.format.sample_rate = 22050;
    } else if (codec_data.sample_rate_ls >> 2 & 1) {
        r.format.sample_rate = 24000;
    } else if (codec_data.sample_rate_ls >> 1 & 1) {
        r.format.sample_rate = 32000;
    } else if (codec_data.sample_rate_ls & 1) {
        r.format.sample_rate = 44100;
    } else if (codec_data.chan_sample_rate >> 7 & 1) {
        r.format.sample_rate = 48000;
    } else if (codec_data.chan_sample_rate >> 6 & 1) {
        r.format.sample_rate = 64000;
    } else if (codec_data.chan_sample_rate >> 5 & 1) {
        r.format.sample_rate = 88200;
    } else if (codec_data.chan_sample_rate >> 4 & 1) {
        r.format.sample_rate = 96000;
    } else {
        r.error = -3;
        return r;
    }
    
    if (codec_data.chan_sample_rate >> 2 & 1) {
        r.format.n_channels = 2;
    } else if (codec_data.chan_sample_rate >> 3 & 1) {
        r.format.n_channels = 1;
    } else {
        r.error = -4;
        return r;
    }
    r.format.bits_per_sample = 16;
    
    handle->aac = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);

    if (aacDecoder_SetParam(handle->aac, AAC_PCM_MIN_OUTPUT_CHANNELS, r.format.n_channels) != AAC_DEC_OK) {
        r.error = -5;
        return r;
    }
    
    if (aacDecoder_SetParam(handle->aac, AAC_PCM_MAX_OUTPUT_CHANNELS, r.format.n_channels) != AAC_DEC_OK) {
        r.error = -6;
        return r;
    }
    
    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;

    return r;
}

void codec_deinit(bluespy_audiostream_id id) {
    struct AAC_handle* handle = get_handle(id);
    if (handle && handle->aac) {
        aacDecoder_Close(handle->aac);
        handle->aac = NULL;
        handle->in_use = 0;
    }
} 

BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id, 
                                                                 const uint8_t* payload,
                                                                 const uint32_t payload_len,
                                                                 bluespy_event_id event_id) 
{
    bluespy_audio_codec_decoded_audio out = {.data = NULL, .len = 0};

    struct AAC_handle* handle = get_handle(id);
    if (!handle || !handle->aac) {
        return out;
    }

    uint8_t* out_buf = handle->out_buf;

    // RTP sequence number
    uint16_t seq = (uint16_t)payload[2] << 8 | payload[3];

    uint32_t payload_left = payload_len;
    if (payload_len > 0) {
        uint32_t rtp_header_len = 12 + 4 * (payload[0] & 0x0F);

        if (payload_len < rtp_header_len)
            return out;

        payload += rtp_header_len;
        payload_left -= rtp_header_len;
    }

    UINT flags = 0;
    if (((seq - handle->sequence_number) & 0xFFFF) != 1 && handle->sequence_number != (uint32_t)-1)
        flags |= AACDEC_INTR;

    handle->sequence_number = seq;

    uint32_t valid = payload_left;
    uint8_t* uncoded_data = out_buf;
    uint32_t out_data_len = 0;
    uint32_t out_len = sizeof(handle->out_buf);

    while (valid > 0) {
        uint32_t size = valid;
        if (aacDecoder_Fill(handle->aac, (uint8_t**)&payload, &size, &valid) != AAC_DEC_OK)
            return out;

        CStreamInfo* info = aacDecoder_GetStreamInfo(handle->aac);
        if (!info)
            return out;

        uint32_t samples_per_frame = info->frameSize * info->numChannels;
        uint32_t bytes_per_frame = samples_per_frame * sizeof(int16_t);

        if (out_len < bytes_per_frame)
            break;

        if (aacDecoder_DecodeFrame(handle->aac, (int16_t*)uncoded_data, bytes_per_frame / sizeof(int16_t), flags) != AAC_DEC_OK)
            break;

        uncoded_data += bytes_per_frame;
        out_data_len += bytes_per_frame;
        out_len -= bytes_per_frame;
    }

    if (out_data_len > 0) {
        out.data = out_buf;
        out.len  = out_data_len;
        out.source_id = event_id;
    }

    return out;
}

#ifdef __cplusplus 
}
#endif