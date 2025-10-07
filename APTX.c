// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"
#include "freeaptx.h"
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus 
extern "C" { 
#endif

bluespy_audio_codec_lib_info init() { 
    return (bluespy_audio_codec_lib_info){.api_version= 1, .codec_name = "aptX"}; 
}

static struct APTX_handle {
    struct aptx_context* aptx;
    bool hd;
    uint32_t sample_rate;
    uint8_t n_channels;
    uint32_t last_timestamp;
    uint32_t total_samples;
} handle = {.aptx = NULL, .hd = false, .sample_rate = 0, .n_channels = 0, .last_timestamp = 0, .total_samples = 0};

bluespy_audio_codec_init_ret codec_init(bluespy_audiostream_id id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret r = { .error = -1, .format = {0, 0, 0}, .fns = {NULL, NULL} };

    if (info->type != BLUESPY_CODEC_APTX && info->type != BLUESPY_CODEC_APTX_HD)
        return r;

    handle.hd = (info->type == BLUESPY_CODEC_APTX_HD);

    // aptX is always stereo
    r.format.n_channels = 2;
    r.format.bits_per_sample = 16;

    // Parse sample rate from capability byte
    uint8_t cap_byte = info->data.AVDTP.AVDTP_Media_Codec_Specific_Information[0];
    
    if (cap_byte & 0x08) {  // Bit 3: 48kHz
        r.format.sample_rate = 48000;
    } else if (cap_byte & 0x10) {  // Bit 4: 44.1kHz
        r.format.sample_rate = 44100;
    } else {
        // Default to 44.1kHz if nothing specified
        r.format.sample_rate = 44100;
    }

    handle.sample_rate = r.format.sample_rate;
    handle.n_channels  = r.format.n_channels;
    handle.last_timestamp = 0;
    handle.total_samples = 0;

    if (handle.aptx) 
        aptx_finish(handle.aptx);

    handle.aptx = aptx_init(handle.hd);

    if (!handle.aptx)
        return r;

    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;
    
    return r;
}

BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id) {
    if (handle.aptx) {
        aptx_finish(handle.aptx);
        handle.aptx = NULL;
        handle.hd = false;
        handle.sample_rate = 0;
        handle.n_channels = 0;
        handle.last_timestamp = 0;
        handle.total_samples = 0;
    }
}

BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id, 
                                                                 const uint8_t* payload,
                                                                 const uint32_t payload_len,
                                                                 int32_t event_id) {
    static uint8_t  out_buf[32768];      // raw aptX PCM (24-bit packed)
    static int16_t  final_output[32768]; // converted PCM16
    bluespy_audio_codec_decoded_audio out = { .data = NULL, .len = 0 };

    if (payload_len == 0)
        return out;

    // The data is pure aptX without any headers
    const uint8_t* coded_data = payload;
    size_t coded_len = payload_len;

    // Decode aptX
    size_t written_bytes = 0;
    size_t consumed = aptx_decode(handle.aptx,
                                  coded_data,
                                  coded_len,
                                  out_buf,
                                  sizeof(out_buf),
                                  &written_bytes);

    if (written_bytes == 0)
        return out;

    // Convert 24-bit PCM -> 16-bit PCM
    size_t final_samples = 0;
    for (size_t i = 0;
         i + 2 < written_bytes && final_samples < (sizeof(final_output) / sizeof(final_output[0]));
         i += 3) {
        // Read 24-bit little-endian sample
        int32_t s = (int32_t)out_buf[i] |
                    ((int32_t)out_buf[i+1] << 8) |
                    ((int32_t)out_buf[i+2] << 16);
        
        // Sign extend from 24-bit to 32-bit
        if (s & 0x800000) {
            s |= 0xFF000000;
        }
        
        // Convert to 16-bit by taking the upper 16 bits of the 24-bit value
        final_output[final_samples++] = (int16_t)(s >> 8);
    }

    out.data = (uint8_t*)final_output;
    out.len  = final_samples * sizeof(int16_t);
    out.has_metadata = true;
    out.source_id = event_id;

    // Update sample counter for debugging
    handle.total_samples += final_samples / handle.n_channels;

    return out;
}

#ifdef __cplusplus 
}
#endif