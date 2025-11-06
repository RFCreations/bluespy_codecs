// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)


#include "bluespy_codec_interface.h"
#include "codec_structures.h"

#include "freeaptx.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_STREAMS 16

typedef struct APTX_handle {
    struct aptx_context* aptx;
    bool      hd;
    uint32_t  sample_rate;
    uint8_t   n_channels;
    uint32_t  last_timestamp;
    uint32_t  total_samples;
    bluespy_audiostream_id stream_id;
    bool      in_use;
    uint8_t   out_buf[32768];      /* 24-bit PCM from Freeaptx */
    int16_t   final_output[32768]; /* 16-bit PCM after conversion */
} APTX_handle;

static APTX_handle handles[MAX_STREAMS] = {0};

static APTX_handle* get_handle(bluespy_audiostream_id id)
{
    // Check if we already have a handle for this ID
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (handles[i].in_use && handles[i].stream_id == id) {
            return &handles[i];
        }
    }
    
    // Allocate a new handle
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!handles[i].in_use) {
            memset(&handles[i], 0, sizeof(APTX_handle));
            handles[i].in_use = true;
            handles[i].stream_id = id;
            return &handles[i];
        }
    }
    return NULL;
}

/* --------------------------------------------------------------------------
 * Library info
 * -------------------------------------------------------------------------- */
BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void)
{
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name  = "aptX"
    };
}

/* --------------------------------------------------------------------------
 * new_codec_stream()
 * -------------------------------------------------------------------------- */
BLUESPY_CODEC_API bluespy_audio_codec_init_ret
new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret r = { .error = -1, .format = {0,0,0}, .fns = {NULL,NULL} };

    const AVDTP_Service_Capabilities_Media_Codec_t* cap =
        (const AVDTP_Service_Capabilities_Media_Codec_t*) info->config;
    
    if (!cap || cap->Media_Codec_Type != AVDTP_Codec_Vendor_Specific)
        return r;

    uint32_t vendor_id =
        (cap->Media_Codec_Specific_Information[3] << 24) |
        (cap->Media_Codec_Specific_Information[2] << 16) |
        (cap->Media_Codec_Specific_Information[1] <<  8) |
         cap->Media_Codec_Specific_Information[0];
    uint8_t vendor_codec_id = cap->Media_Codec_Specific_Information[4];

    if (vendor_id != 0x0000004F ||
        (vendor_codec_id != 0x01 && vendor_codec_id != 0x02))
        return r;

    APTX_handle* handle = get_handle(id);
    if (!handle) { 
        r.error = -10; 
        return r; 
    }

    handle->hd = (vendor_codec_id == 0x02);
    
    // aptX is always stereo
    handle->n_channels = 2;
    r.format.n_channels = 2;
    r.format.bits_per_sample = 16;
    
    // Parse sample rate from capability byte
    uint8_t cap_byte = cap->Media_Codec_Specific_Information[5];
    
    if (cap_byte & 0x08) {  // Bit 3: 48kHz
        r.format.sample_rate = 48000;
    } else if (cap_byte & 0x10) {  // Bit 4: 44.1kHz
        r.format.sample_rate = 44100;
    } else {
        // Default to 44.1kHz if nothing specified
        r.format.sample_rate = 48000;
    }

    handle->sample_rate = r.format.sample_rate;
    handle->last_timestamp = 0;
    handle->total_samples = 0;

    if (handle->aptx) 
        aptx_finish(handle->aptx);

    handle->aptx = aptx_init(handle->hd);

    if (!handle->aptx) {
        handle->in_use = false;
        return r;
    }

    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;
    
    return r;
}

/* --------------------------------------------------------------------------
 * codec_deinit()
 * -------------------------------------------------------------------------- */
BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id)
{
    APTX_handle* handle = get_handle(id);
    if (!handle) return;

    if (handle->aptx) {
        aptx_finish(handle->aptx);
        handle->aptx = NULL;
    }
    handle->in_use = false;
}

/* --------------------------------------------------------------------------
 * codec_decode()
 * -------------------------------------------------------------------------- */
BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio
codec_decode(bluespy_audiostream_id id,
             const uint8_t* payload,
             uint32_t payload_len,
             bluespy_event_id event_id)
{
    bluespy_audio_codec_decoded_audio out = { .data = NULL, .len = 0, .source_id = event_id };
    
    APTX_handle* handle = get_handle(id);
    if (!handle || !handle->aptx || payload_len == 0)
        return out;

    // The data is pure aptX without any headers (like the old working version)
    const uint8_t* coded_data = payload;
    size_t coded_len = payload_len;

    // Decode aptX
    size_t written_bytes = 0;
    size_t consumed = aptx_decode(handle->aptx,
                                  coded_data,
                                  coded_len,
                                  handle->out_buf,
                                  sizeof(handle->out_buf),
                                  &written_bytes);

    if (written_bytes == 0)
        return out;

    // Convert 24-bit PCM -> 16-bit PCM
    size_t final_samples = 0;
    for (size_t i = 0;
         i + 2 < written_bytes && final_samples < (sizeof(handle->final_output) / sizeof(handle->final_output[0]));
         i += 3) {
        // Read 24-bit little-endian sample
        int32_t s = (int32_t)handle->out_buf[i] |
                    ((int32_t)handle->out_buf[i+1] << 8) |
                    ((int32_t)handle->out_buf[i+2] << 16);
        
        // Sign extend from 24-bit to 32-bit
        if (s & 0x800000) {
            s |= 0xFF000000;
        }
        
        // Convert to 16-bit by taking the upper 16 bits of the 24-bit value
        handle->final_output[final_samples++] = (int16_t)(s >> 8);
    }

    out.data = (uint8_t*)handle->final_output;
    out.len  = final_samples * sizeof(int16_t);
    out.source_id = event_id;

    // Update sample counter for debugging
    handle->total_samples += final_samples / handle->n_channels;

    return out;
}

#ifdef __cplusplus
}
#endif