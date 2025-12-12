// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file aptX.cpp
 * @brief aptX/aptX HD codec plugin for blueSPY
 *
 * Implements aptX and aptX HD decoding for AVDTP/A2DP Classic
 * audio streams using the freeaptx library.
 */

#include "bluespy_codec_interface.h"
#include "codec_structures.h"
extern "C" {
    #include "freeaptx.h"
}

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/*------------------------------------------------------------------------------
 * Constants
 *----------------------------------------------------------------------------*/

#define MAX_STREAMS             16
#define PCM_BUFFER_SAMPLES      8192    /* Max 16-bit samples per decode */
#define RAW_BUFFER_BYTES        (PCM_BUFFER_SAMPLES * 3)  /* 24-bit input */

/** Qualcomm Vendor ID (little-endian: 0x4F000000) */
#define VENDOR_ID_QUALCOMM      0x0000004F

/** Qualcomm aptX Codec IDs */
#define CODEC_ID_APTX           0x01
#define CODEC_ID_APTX_HD        0x02

/** aptX capability byte bit positions */
#define APTX_SAMP_FREQ_48000    0x08
#define APTX_SAMP_FREQ_44100    0x10

/** Bytes per sample in freeaptx output (24-bit) */
#define BYTES_PER_RAW_SAMPLE    3

/*------------------------------------------------------------------------------
 * Types
 *----------------------------------------------------------------------------*/

/**
 * @brief Per-stream aptX decoder state
 */
typedef struct {
    bluespy_audiostream_id stream_id;
    bool in_use;
    bool initialized;

    /* freeaptx decoder context */
    struct aptx_context* decoder;
    bool is_hd;

    /* Stream configuration */
    uint32_t sample_rate;
    uint8_t  channels;

    /* Statistics */
    uint32_t total_frames;

    /* Output buffers */
    uint8_t raw_buffer[RAW_BUFFER_BYTES];   /* 24-bit PCM from freeaptx */
    int16_t pcm_buffer[PCM_BUFFER_SAMPLES]; /* Converted 16-bit PCM */
} aptX_stream;

/*------------------------------------------------------------------------------
 * Static Data
 *----------------------------------------------------------------------------*/

static aptX_stream g_streams[MAX_STREAMS];

/*------------------------------------------------------------------------------
 * Stream Handle Management
 *----------------------------------------------------------------------------*/

/**
 * @brief Find existing stream by ID
 */
static aptX_stream* stream_find(bluespy_audiostream_id id)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (g_streams[i].in_use && g_streams[i].stream_id == id) {
            return &g_streams[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate a new stream slot
 */
static aptX_stream* stream_allocate(bluespy_audiostream_id id)
{
    /* Check if already exists */
    aptX_stream* existing = stream_find(id);
    if (existing) {
        return existing;
    }

    /* Find free slot */
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (!g_streams[i].in_use) {
            memset(&g_streams[i], 0, sizeof(g_streams[i]));
            g_streams[i].in_use = true;
            g_streams[i].stream_id = id;
            return &g_streams[i];
        }
    }
    return NULL;
}

/**
 * @brief Release stream and free resources
 */
static void stream_release(aptX_stream* stream)
{
    if (!stream || !stream->in_use) {
        return;
    }

    if (stream->decoder) {
        aptx_finish(stream->decoder);
    }

    memset(stream, 0, sizeof(*stream));
}

/*------------------------------------------------------------------------------
 * Configuration Parsing
 *----------------------------------------------------------------------------*/

/**
 * @brief Read little-endian uint32 from buffer
 */
static inline uint32_t read_le32(const uint8_t* p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

/**
 * @brief Check if configuration is for aptX or aptX HD
 *
 * @param cap        AVDTP Media Codec capability structure
 * @param is_hd_out  Output: true if aptX HD, false if standard aptX
 * @return true if this is an aptX/aptX HD configuration
 */
static bool is_aptx_config(const AVDTP_Service_Capabilities_Media_Codec_t* cap, bool* is_hd_out)
{
    if (cap->Media_Codec_Type != AVDTP_Codec_Vendor_Specific) {
        return false;
    }

    const uint8_t* info = cap->Media_Codec_Specific_Information;
    uint32_t vendor_id = read_le32(info);
    uint8_t codec_id = info[4];

    if (vendor_id != VENDOR_ID_QUALCOMM) {
        return false;
    }

    if (codec_id == CODEC_ID_APTX) {
        *is_hd_out = false;
        return true;
    }

    if (codec_id == CODEC_ID_APTX_HD) {
        *is_hd_out = true;
        return true;
    }

    return false;
}

/**
 * @brief Parse sample rate from aptX capability byte
 *
 * @param cap_byte  Capability byte from Media_Codec_Specific_Information[5]
 * @return Sample rate in Hz
 */
static uint32_t parse_sample_rate(uint8_t cap_byte)
{
    if (cap_byte & APTX_SAMP_FREQ_48000) {
        return 48000;
    }
    if (cap_byte & APTX_SAMP_FREQ_44100) {
        return 44100;
    }
    /* Default to 48kHz if not specified */
    return 48000;
}

/*------------------------------------------------------------------------------
 * Audio Conversion
 *----------------------------------------------------------------------------*/

/**
 * @brief Convert 24-bit little-endian PCM to 16-bit signed PCM
 *
 * The freeaptx library outputs 24-bit samples. This function converts
 * them to 16-bit by taking the upper 16 bits of each 24-bit sample.
 *
 * @param src           Source buffer containing 24-bit LE samples
 * @param src_bytes     Number of bytes in source buffer
 * @param dst           Destination buffer for 16-bit samples
 * @param max_samples   Maximum samples that fit in destination
 * @return Number of 16-bit samples written
 */
static size_t convert_24bit_to_16bit(const uint8_t* src, size_t src_bytes, int16_t* dst, size_t max_samples)
{
    size_t samples_written = 0;

    for (size_t i = 0; i + 2 < src_bytes && samples_written < max_samples; i += 3) {
        /* Read 24-bit little-endian sample */
        int32_t sample = (int32_t)src[i] |
                         ((int32_t)src[i + 1] << 8) |
                         ((int32_t)src[i + 2] << 16);

        /* Sign-extend from 24-bit to 32-bit */
        if (sample & 0x00800000) {
            sample |= 0xFF000000;
        }

        /* Convert to 16-bit by dropping lower 8 bits */
        dst[samples_written++] = (int16_t)(sample >> 8);
    }

    return samples_written;
}

/*------------------------------------------------------------------------------
 * API Implementation
 *----------------------------------------------------------------------------*/

extern "C" {

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void)
{
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name = "aptX"
    };
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret ret = {
        .error = -1,
        .format = {0},
        .fns = {0}
    };

    /* Only handle AVDTP container */
    if (!info || info->container != BLUESPY_CODEC_AVDTP) {
        return ret;
    }

    /* Validate configuration */
    const AVDTP_Service_Capabilities_Media_Codec_t* cap = (const AVDTP_Service_Capabilities_Media_Codec_t*)info->config;

    if (!cap) {
        return ret;
    }

    /* Check if this is aptX or aptX HD */
    bool is_hd;
    if (!is_aptx_config(cap, &is_hd)) {
        return ret;
    }

    /* Allocate stream handle */
    aptX_stream* stream = stream_allocate(stream_id);
    if (!stream) {
        ret.error = -2;
        return ret;
    }

    /* Clean up any existing decoder */
    if (stream->decoder) {
        aptx_finish(stream->decoder);
        stream->decoder = NULL;
    }

    /* Parse configuration */
    stream->is_hd = is_hd;
    stream->sample_rate = parse_sample_rate(cap->Media_Codec_Specific_Information[5]);
    stream->channels = 2;  /* aptX is always stereo */

    /* Create freeaptx decoder */
    stream->decoder = aptx_init(is_hd);
    if (!stream->decoder) {
        stream_release(stream);
        ret.error = -3;
        return ret;
    }

    stream->initialized = true;

    /* Success */
    ret.error = 0;
    ret.format.sample_rate = stream->sample_rate;
    ret.format.n_channels = stream->channels;
    ret.format.bits_per_sample = 16;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;

    return ret;
}

BLUESPY_CODEC_API void codec_decode(bluespy_audiostream_id stream_id, const uint8_t* payload, uint32_t payload_len, bluespy_event_id event_id, uint64_t sequence_number)
{
    (void)sequence_number;

    aptX_stream* stream = stream_find(stream_id);
    if (!stream || !stream->initialized || !stream->decoder) {
        return;
    }

    if (!payload || payload_len == 0) {
        return;
    }

    /*
     * aptX packets are raw codec frames without RTP or other headers.
     * The entire payload is encoded aptX data.
     */
    size_t raw_bytes_written = 0;
    size_t bytes_consumed = aptx_decode(
        stream->decoder,
        payload,
        payload_len,
        stream->raw_buffer,
        sizeof(stream->raw_buffer),
        &raw_bytes_written);

    (void)bytes_consumed;  /* Could be used for error checking */

    if (raw_bytes_written == 0) {
        return;
    }

    /* Convert 24-bit output to 16-bit */
    size_t samples = convert_24bit_to_16bit(
        stream->raw_buffer,
        raw_bytes_written,
        stream->pcm_buffer,
        PCM_BUFFER_SAMPLES);

    if (samples == 0) {
        return;
    }

    /* Deliver decoded audio */
    uint32_t pcm_bytes = (uint32_t)(samples * sizeof(int16_t));
    bluespy_add_continuous_audio((const uint8_t*)stream->pcm_buffer, pcm_bytes, event_id);

    /* Update statistics */
    stream->total_frames += (uint32_t)(samples / stream->channels);
}

BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id stream_id)
{
    aptX_stream* stream = stream_find(stream_id);
    if (stream) {
        stream_release(stream);
    }
}

} // end extern "C"