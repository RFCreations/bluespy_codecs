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
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION, 
        .codec_name = "aptX"
    }; 
}

#define MAX_STREAMS 16

static struct APTX_handle {
    struct aptx_context* aptx;
    bool hd;
    uint32_t sample_rate;
    uint8_t n_channels;
    uint32_t last_timestamp;
    uint32_t total_samples;
    bluespy_audiostream_id stream_id;
    int in_use;
    uint8_t out_buf[32768];
    int16_t final_output[32768];
} handles[MAX_STREAMS] = {0};

static struct APTX_handle* get_handle(bluespy_audiostream_id id) {
    // Check if we already have a handle for this ID
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
            handles[i].aptx = NULL;
            handles[i].hd = false;
            handles[i].sample_rate = 0;
            handles[i].n_channels = 0;
            handles[i].last_timestamp = 0;
            handles[i].total_samples = 0;
            return &handles[i];
        }
    }
    
    return NULL;
}

bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret r = { .error = -1, .format = {0, 0, 0}, .fns = {NULL, NULL} };

    const AVDTP_Service_Capabilities_Media_Codec_t* cap = info->data.AVDTP.Media_Codec_Capability;
    
    if(cap->Media_Codec_Type != AVDTP_Codec_Vendor_Specific)
        return r;

    uint32_t vendor_id =
        (cap->Media_Codec_Specific_Information[3] << 24) |
        (cap->Media_Codec_Specific_Information[2] << 16) |
        (cap->Media_Codec_Specific_Information[1] << 8)  |
        (cap->Media_Codec_Specific_Information[0]);

    uint8_t vendor_codec_id = cap->Media_Codec_Specific_Information[4];

    struct APTX_handle* handle = get_handle(id);
    if (!handle) {
        r.error = -10; // Too many concurrent streams
        return r;
    }

    if (vendor_id == 0x0000004F && vendor_codec_id == 0x01) {
        handle->hd = false; // aptX
    } else if (vendor_id == 0x0000004F && vendor_codec_id == 0x02) {
        handle->hd = true; // aptX HD
    } else {
        handle->in_use = 0; // Release the handle
        return r;
    }
    
    r.format.n_channels = 2;
    r.format.bits_per_sample = 16;

    uint8_t cap_byte = cap->Media_Codec_Specific_Information[0];
    
    if (cap_byte & 0x08) {
        r.format.sample_rate = 48000;
    } else if (cap_byte & 0x10) {
        r.format.sample_rate = 44100;
    } else {
        r.format.sample_rate = 44100;
    }

    handle->sample_rate = r.format.sample_rate;
    handle->n_channels  = r.format.n_channels;

    if (handle->aptx) 
        aptx_finish(handle->aptx);

    handle->aptx = aptx_init(handle->hd);

    if (!handle->aptx) {
        handle->in_use = 0;
        return r;
    }

    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;
    
    return r;
}

BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id) {
    struct APTX_handle* handle = get_handle(id);
    if (handle && handle->aptx) {
        aptx_finish(handle->aptx);
        handle->aptx = NULL;
        handle->in_use = 0;
    }
}

BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id, 
                                                                 const uint8_t* payload,
                                                                 const uint32_t payload_len,
                                                                 bluespy_event_id event_id) {
    bluespy_audio_codec_decoded_audio out = { .data = NULL, .len = 0 };

    struct APTX_handle* handle = get_handle(id);
    if (!handle || !handle->aptx) {
        return out;
    }

    if (payload_len == 0)
        return out;

    uint8_t* out_buf = handle->out_buf;
    int16_t* final_output = handle->final_output;

    const uint8_t* coded_data = payload;
    size_t coded_len = payload_len;

    size_t written_bytes = 0;
    size_t consumed = aptx_decode(handle->aptx,
                                  coded_data,
                                  coded_len,
                                  out_buf,
                                  sizeof(handle->out_buf),
                                  &written_bytes);

    if (written_bytes == 0)
        return out;

    size_t final_samples = 0;
    size_t max_samples = sizeof(handle->final_output) / sizeof(handle->final_output[0]);
    
    for (size_t i = 0; i + 2 < written_bytes && final_samples < max_samples; i += 3) {
        int32_t s = (int32_t)out_buf[i] |
                    ((int32_t)out_buf[i+1] << 8) |
                    ((int32_t)out_buf[i+2] << 16);
        
        if (s & 0x800000) {
            s |= 0xFF000000;
        }
        
        final_output[final_samples++] = (int16_t)(s >> 8);
    }

    out.data = (uint8_t*)final_output;
    out.len  = final_samples * sizeof(int16_t);
    out.source_id = event_id;

    handle->total_samples += final_samples / handle->n_channels;

    return out;
}

#ifdef __cplusplus 
}
#endif