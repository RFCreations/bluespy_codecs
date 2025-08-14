// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"

#include "freeaptx.h"
#include <stdint.h>
#include <string.h>

// TODO aptx LL ??

bluespy_audio_codec_lib_info init() { return (bluespy_audio_codec_lib_info){.api_ver= 1, .codec_name = "aptX"}; }

static struct APTX_handle {
    struct aptx_context* aptx;
    bool hd;
    uint32_t sample_rate;
    uint8_t n_channels;
} handle = {.aptx = nullptr, .hd = false, .sample_rate = 0, .n_channels = 0};

bluespy_audio_codec_init_ret codec_init(bluespy_audiostream_id id, const bluespy_audio_codec_info* info) {
    
    bluespy_audio_codec_init_ret r = {.ret = -1, .format = {0, 0, 0}, .fns = {NULL, NULL}};
    
    bool hd = false;
    if (info->type == BLUESPY_AVDTP_APTX || info->type == BLUESPY_AVDTP_APTX_HD) {
        bool hd = (info->type == BLUESPY_AVDTP_APTX_HD);
    } else {
         return r;
    }
        
    if(info->data.AVDTP.len < 7) { // TODO I think this is right but I don't understand it
        r.ret = -2;
        return r;
    }
    
    struct { // TODO not sure where this information comes from? So the logic below is probably wrong
        uint8_t object_type;
        uint8_t channels_sample_rate;
        uint8_t bit_rate_ls_vbr;
        uint8_t bit_rate_mid;
        uint8_t bit_rate_ms;
    } codec_data;

    memcpy(&codec_data, info->data.AVDTP.AVDTP_Media_Codec_Specific_Information, 5);


    if ((channels_sample_rate & 0xF) == 2) {
        r.format.n_channels = 2;
    } else {
        return r;
    }

    switch (channels_sample_rate >> 4) {
        case 1:  r.format.sample_rate = 48000; break;
        case 2:  r.format.sample_rate = 44100; break;
        case 4:  r.format.sample_rate = 32000; break;
        case 8:  r.format.sample_rate = 16000; break;
        default: return r;
    }

    r.format.bits_per_sample = 16;

    if (handle.aptx) aptx_finish(handle.aptx);
    handle.aptx = aptx_init(hd);
    handle.hd = hd;


    r.ret = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = NULL;
    
    return r;
}


BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id, const uint8_t* payload, const uint32_t payload_len) {
    static uint8_t out_buf[4096 * 3];
    bluespy_audio_codec_decoded_audio out = {.data = NULL, .len = 0};
    size_t uncoded_bytes = 0;

    const uint8_t* coded_data = payload;
    size_t coded_len = payload_len;

    if (coded_len > 0 && handle.hd) {
        uint32_t rtp_header_len = 12 + 4 * (*coded_data & 0xF);
        if (coded_len < rtp_header_len)
            return out;
        coded_data += rtp_header_len;
        coded_len -= rtp_header_len;
    }

    size_t out_total_samples = 8 * (handle.hd ? coded_len / 6 : coded_len / 4);
    if (out_total_samples == 0 || out_total_samples > sizeof(out_buf) / 3)
        return out;


    size_t written = 0;
    aptx_decode(handle.aptx, coded_data, coded_len, out_buf, out_total_samples * 3, &written);

    if (written == 0)
        return out;

    // Convert to 16-bit PCM
    static int16_t final_output[4096 * 2];
    size_t final_samples = 0;
    for (size_t i; i + 2 < written && final_samples < sizeof(final_output)/sizeof(final_output[0]); i += 3) {
        final_output[final_samples++] = (int16_t)out_buf[i + 1] | ((int16_t)out_buf[i + 2] << 8);
    }

    out.data = (uint8_t*)final_output;
    out.len = final_samples * sizeof(int16_t);
    return out;
}