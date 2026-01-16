// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file APTX.cpp
 * @brief aptX/aptX HD codec plugin for blueSPY
 */

#include "bluespy_codec_interface.h"
#include "codec_structures.h"

extern "C" {
#include "freeaptx.h"
}

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------------
 * Constants
 *----------------------------------------------------------------------------*/

#define PCM_BUFFER_SAMPLES 8192                   /* Max 16-bit samples per decode */
#define RAW_BUFFER_BYTES (PCM_BUFFER_SAMPLES * 3) /* 24-bit input */

/** Qualcomm Vendor ID (little-endian: 0x4F000000) */
#define VENDOR_ID_QUALCOMM 0x0000004F

/** Qualcomm aptX Codec IDs */
#define CODEC_ID_APTX 0x01
#define CODEC_ID_APTX_HD 0x02

/* * Standard aptX Sample Rate Values (Upper Nibble of Byte 6 of Media_Codec_Specific_Information) */
#define APTX_FREQ_VAL_48000 0x1
#define APTX_FREQ_VAL_44100 0x2
#define APTX_FREQ_VAL_32000 0x4
#define APTX_FREQ_VAL_16000 0x8

/*------------------------------------------------------------------------------
 * Types
 *----------------------------------------------------------------------------*/

/**
 * @brief aptX decoder state
 */
typedef struct {
    bluespy_audiostream_id parent_stream_id;
    bool initialized;

    struct aptx_context* decoder;
    bool is_hd;

    uint32_t sample_rate;
    uint8_t channels;
    uint32_t total_frames;

    uint8_t raw_buffer[RAW_BUFFER_BYTES];
    int16_t pcm_buffer[PCM_BUFFER_SAMPLES];
} aptX_stream;

/*------------------------------------------------------------------------------
 * Helper Functions
 *----------------------------------------------------------------------------*/

/**
 * @brief Read little-endian uint32 from buffer
 */
static inline uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Check if configuration is for aptX codec
 *
 * @param cap  AVDTP Media Codec capability structure
 * @param is_hd_out true if codec is aptX HD
 *
 * @return true if this is an aptX configuration
 */
static bool is_aptx_config(const AVDTP_Service_Capabilities_Media_Codec_t* cap, bool* is_hd_out) {
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
 * @brief Parse sample rate from aptX configuration
 * - On Little Endian systems, Byte 6 contains:
 * - Lower 4 bits: Channel Mode
 * - Upper 4 bits: Sampling Frequency
 *
 * @param info   Pointer to the start of Media_Codec_Specific_Information
 * @param len    Length of the info block
 *
 * @return Sample rate in Hz
 */
static uint32_t parse_sample_rate(const uint8_t* info, uint32_t len) {
    if (len < 7) {
        return 44100;
    }

    uint8_t freq_nibble = (info[6] >> 4) & 0x0F;

    if (freq_nibble & APTX_FREQ_VAL_44100) {
        return 44100;
    }
    if (freq_nibble & APTX_FREQ_VAL_48000) {
        return 48000;
    }
    if (freq_nibble & APTX_FREQ_VAL_32000) {
        return 32000;
    }
    if (freq_nibble & APTX_FREQ_VAL_16000) {
        return 16000;
    }

    return 44100;
}

/**
 * @brief Convert 24-bit Little-Endian PCM to 16-bit PCM.
 * The freeaptx library produces 24-bit audio samples (stored as 3 consecutive bytes).
 * This function converts them to 16-bit samples by discarding the least significant
 * byte (truncation) and preserving the sign.
 *
 * @param[in]  src          Pointer to the source buffer containing 24-bit packed samples.
 * @param[in]  src_bytes    Total size of the source buffer in bytes.
 * @param[out] dst          Pointer to the destination buffer for 16-bit samples.
 * @param[in]  max_samples  The maximum number of 16-bit samples the dst buffer can hold.
 *
 * @return The actual number of samples written to the destination buffer.
 */
static size_t convert_24bit_to_16bit(const uint8_t* src, size_t src_bytes, int16_t* dst,
                                     size_t max_samples) {
    size_t samples_written = 0;
    for (size_t i = 0; i + 2 < src_bytes && samples_written < max_samples; i += 3) {
        int32_t sample = (int32_t)src[i] | ((int32_t)src[i + 1] << 8) | ((int32_t)src[i + 2] << 16);
        if (sample & 0x00800000) {
            sample |= 0xFF000000;
        }
        dst[samples_written++] = (int16_t)(sample >> 8);
    }
    return samples_written;
}

/*------------------------------------------------------------------------------
 * API Implementation
 *----------------------------------------------------------------------------*/

extern "C" {

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void) {
    return bluespy_audio_codec_lib_info{.api_version = BLUESPY_AUDIO_API_VERSION,
                                        .codec_name = "aptX"};
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret
new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret ret = {
        .error = -1, .format = {0}, .fns = {0}, .context_handle = 0};

    /* Validate Config */
    if (!info || info->container != BLUESPY_CODEC_AVDTP) {
        return ret;
    }
    const AVDTP_Service_Capabilities_Media_Codec_t* cap =
        (const AVDTP_Service_Capabilities_Media_Codec_t*)info->config;
    if (!cap) {
        return ret;
    }
    bool is_hd;
    if (!is_aptx_config(cap, &is_hd)) {
        return ret;
    }

    /* Dry run to allow the host to check if this codec format is supported */
    if (stream_id == BLUESPY_ID_INVALID) {
        ret.error = 0;
        return ret;
    }

    /* Allocate State */
    aptX_stream* stream = (aptX_stream*)calloc(1, sizeof(aptX_stream));
    if (!stream) {
        ret.error = -2;
        return ret;
    }

    if (stream->decoder) {
        aptx_finish(stream->decoder);
        stream->decoder = NULL;
    }

    /* Init Decoder */
    stream->is_hd = is_hd;
    stream->sample_rate = parse_sample_rate(cap->Media_Codec_Specific_Information, info->config_len);
    stream->channels = 2;
    stream->decoder = aptx_init(is_hd);
    if (!stream->decoder) {
        free(stream);
        ret.error = -3;
        return ret;
    }

    stream->initialized = true;
    stream->total_frames = 0;

    ret.error = 0;
    ret.context_handle = (uintptr_t)stream;

    ret.format.sample_rate = stream->sample_rate;
    ret.format.n_channels = stream->channels;
    ret.format.sample_format = BLUESPY_AUDIO_FORMAT_S16_LE;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;
    return ret;
}

BLUESPY_CODEC_API void codec_decode(uintptr_t context, const uint8_t* payload, uint32_t payload_len,
                                    bluespy_event_id event_id, uint64_t sequence_number) {
    (void)sequence_number;

    aptX_stream* stream = (aptX_stream*)context;
    if (!stream || !stream->initialized || !stream->decoder) {
        return;
    }
    if (!payload || payload_len == 0) {
        return;
    }

    const uint32_t missing_samples = 0; // NOTE this plugin assumes RAW aptX frames (no RTP headers)
                                        // so gap detection is disabled (missing_samples = 0)

    /* Decode (Directly on payload, no header stripping) */
    size_t raw_bytes_written = 0;
    size_t bytes_consumed = aptx_decode(stream->decoder, payload, payload_len, stream->raw_buffer,
                                        sizeof(stream->raw_buffer), &raw_bytes_written);

    (void)bytes_consumed;

    if (raw_bytes_written == 0) {
        return;
    }

    /* Convert and Deliver */
    size_t samples = convert_24bit_to_16bit(stream->raw_buffer, raw_bytes_written,
                                            stream->pcm_buffer, PCM_BUFFER_SAMPLES);

    if (samples > 0) {
        uint32_t pcm_bytes = (uint32_t)(samples * sizeof(int16_t));

        // Pass 0 for missing_samples
        bluespy_add_audio((const uint8_t*)stream->pcm_buffer, pcm_bytes, event_id, missing_samples);

        stream->total_frames += (uint32_t)(samples / stream->channels);
    }
}

BLUESPY_CODEC_API void codec_deinit(uintptr_t context) {
    aptX_stream* stream = (aptX_stream*)context;
    if (stream) {
        if (stream->decoder) {
            aptx_finish(stream->decoder);
        }
        free(stream);
    }
}

} // end extern "C"