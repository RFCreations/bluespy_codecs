// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

// #include "bluespy.h"
// #include "bluespy_codec_defs.h"
#include "bluespy_codec_interface.h"

#include "aacdecoder_lib.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus 
extern "C" { 
#endif

bluespy_audio_codec_lib_info init() { return (bluespy_audio_codec_lib_info){.api_version= 1, .codec_name = "AAC"}; }

static struct AAC_handle{
    HANDLE_AACDECODER aac;
    uint32_t sequence_number;
}handle = {.aac = NULL, .sequence_number = -1};

bluespy_audio_codec_init_ret codec_init(bluespy_audiostream_id id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret r = {.ret = -1, .format = {0, 0, 0}, .fns = {NULL, NULL}};
    if(info->type != BLUESPY_AVDTP_AAC)
        return r;
    
    if(info->data.AVDTP.len < 6) {
        r.ret = -2;    
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

    memcpy(&codec_data, info->data.AVDTP.AVDTP_Media_Codec_Specific_Information, 6);

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
        r.ret = -3;
        return r;
    }
    
    if (codec_data.chan_sample_rate >> 2 & 1) {
        r.format.n_channels = 2;
    } else if (codec_data.chan_sample_rate >> 3 & 1) {
        r.format.n_channels = 1;
    } else {
        r.ret = -4;
        return r;
    }
    r.format.bits_per_sample = 16;
    
    handle.aac = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);

    if (aacDecoder_SetParam(handle.aac, AAC_PCM_MIN_OUTPUT_CHANNELS, r.format.n_channels) != AAC_DEC_OK) {
        r.ret = -5;
        return r;
    }
    
    if (aacDecoder_SetParam(handle.aac, AAC_PCM_MAX_OUTPUT_CHANNELS, r.format.n_channels) != AAC_DEC_OK) {
        r.ret = -6;
        return r;
    }
    
    r.ret = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;

    return r;
}

void codec_deinit(bluespy_audiostream_id id) {
    if (handle.aac) {
        aacDecoder_Close(handle.aac);
        handle.aac = NULL;
    }
}

BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id, const uint8_t* payload, const uint32_t payload_len) 
{
    // Make the output buffer larger to handle multiple frames
    static uint8_t out_buf[32768];   // 32 KB
    
    bluespy_audio_codec_decoded_audio out = {.data = NULL, .len = 0};

    // RTP sequence number
    uint16_t seq = (uint16_t)payload[2] << 8 | payload[3];

    uint32_t payload_left = payload_len;
    if (payload_len > 0) {
        // Remove RTP header: 12 bytes + 4*CSRC count
        uint32_t rtp_header_len = 12 + 4 * (payload[0] & 0x0F);

        if (payload_len < rtp_header_len)
            return out;

        payload += rtp_header_len;
        payload_left -= rtp_header_len;
    }

    UINT flags = 0;
    if (((seq - handle.sequence_number) & 0xFFFF) != 1 && handle.sequence_number != (uint32_t)-1)
        flags |= AACDEC_INTR;   // conceal gap

    handle.sequence_number = seq;

    uint32_t valid = payload_left;
    uint8_t* uncoded_data = out_buf;
    uint32_t out_data_len = 0;
    uint32_t out_len = sizeof(out_buf);

    while (valid > 0) {
        uint32_t size = valid;
        if (aacDecoder_Fill(handle.aac, (uint8_t**)&payload, &size, &valid) != AAC_DEC_OK)
            return out;

        CStreamInfo* info = aacDecoder_GetStreamInfo(handle.aac);
        if (!info)
            return out;

        uint32_t samples_per_frame = info->frameSize * info->numChannels;
        uint32_t bytes_per_frame = samples_per_frame * sizeof(int16_t);

        if (out_len < bytes_per_frame)
            break; // prevent overflow

        if (aacDecoder_DecodeFrame(handle.aac, (int16_t*)uncoded_data, bytes_per_frame / sizeof(int16_t), flags) != AAC_DEC_OK)
            break; // conceal on error, break out

        uncoded_data += bytes_per_frame;
        out_data_len += bytes_per_frame;
        out_len -= bytes_per_frame;
    }

    if (out_data_len > 0) {
        out.data = out_buf;
        out.len  = out_data_len;
    }

    return out;
}

#ifdef __cplusplus 
}
#endif