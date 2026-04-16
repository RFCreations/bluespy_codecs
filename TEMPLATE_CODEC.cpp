// Copyright RF Creations Ltd 2026
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file TEMPLATE_CODEC.cpp
 * @brief Template codec plugin for blueSPY
 *
 * Use this file as a skeleton for implementing a new codec plugin.
 * It will also be helpful to look at the working implementations in this repo which include
 * AAC.cpp, APTX.cpp, LDAC.cpp, and LC3.cpp.
 *
 * Each codec plugin must implement the following exported functions:
 *   - `init()`
 *   - `new_codec_stream()`
 *   - `codec_decode()`
 *   - `codec_deinit()`
 *
 * The host (blueSPY) will:
 *   1. Load the shared library (DLL/.so/.dylib).
 *   2. Call init() once to verify the codec name and API version.
 *   3. Call new_codec_stream() when a new captured audio session begins.
 *   4. Call codec_decode() repeatedly with encoded data packets/frames.
 *   5. Call codec_deinit() when the stream ends or resets.
 *
 * Replace the placeholder logic below with your actual codec implementation.
 */

#include "bluespy_codec_interface.h"
#include "bluespy_codec_utils.h"
#include "codec_structures.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------------
 * Constants & Offsets
 *----------------------------------------------------------------------------*/

/** Example PCM buffer size (in S16 samples) */
#define TEMPLATE_PCM_BUFFER_SAMPLES 8192

/** Example Vendor / Codec IDs (Replace with actual values) */
#define VENDOR_ID_EXAMPLE 0x00000000
#define CODEC_ID_EXAMPLE 0xFF

/** Example Configuration Offsets (Encourage abstraction of magic numbers) */
#define CONFIG_LEN_MIN 6
#define OFFSET_SAMPLE_RATE 4

/*------------------------------------------------------------------------------
 * Data Structures
 *----------------------------------------------------------------------------*/

/**
 * @brief Per-stream codec decoder state.
 *
 * Each active stream has one instance of this structure. Put any
 * codec-specific decoder handles, context state, or buffers here.
 */
typedef struct {
    bluespy_audiostream_id parent_stream_id;
    bool initialized;

    /* Sequence Tracking for Gap Detection */
    bool have_seq;
    uint16_t last_seq;
    uint32_t samples_per_frame; /* Used to calculate missing_samples on packet loss */

    /* Example configuration */
    uint32_t sample_rate;
    uint8_t channels;

    /* Example codec-specific handle (replace with real type) */
    void* decoder_handle;

    /* PCM output buffer (16-bit samples, interleaved) */
    int16_t pcm_buffer[TEMPLATE_PCM_BUFFER_SAMPLES];

} TEMPLATE_stream;

/*------------------------------------------------------------------------------
 * Codec Configuration Parsing
 *----------------------------------------------------------------------------*/

/**
 * @brief Parse configuration data from blueSPY container.
 */
static bool parse_codec_config(const bluespy_audio_codec_info* info, TEMPLATE_stream* stream) {
    if (!info || !info->config || info->config_len == 0) {
        return false;
    }

    /* Example of parsing logic based on container */
    switch (info->container) {
    case BLUESPY_CODEC_AVDTP:
        /* Typical for Classic A2DP codecs — parse AVDTP capabilities here */
        stream->sample_rate = 44100;
        stream->channels = 2;
        break;

    case BLUESPY_CODEC_CIS:
    case BLUESPY_CODEC_BIS:
        /* LE Audio (Isochronous Stream) configuration (LTV parsing) */
        stream->sample_rate = 48000;
        stream->channels = 1;
        break;

    default:
        return false;
    }

    return true;
}

/*------------------------------------------------------------------------------
 * Public API Functions (required)
 *----------------------------------------------------------------------------*/

extern "C" {

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void) {
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name = "TEMPLATE_CODEC" /* TODO: Change this name */
    };
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret
new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret ret = {
        .error = -1, .format = {0}, .fns = {0}, .context_handle = 0};

    /* Parameter validation */
    if (!info || !info->config || info->config_len == 0) {
        return ret;
    }

    /* TODO: Add a check here to ensure this plugin actually supports
     * the requested codec (e.g. check Vendor ID / Codec ID in info->config).
     */

    /* Parse configuration into temporary struct to allow dry-run testing */
    TEMPLATE_stream temp_stream = {0};
    if (!parse_codec_config(info, &temp_stream)) {
        ret.error = -3;
        return ret;
    }

    /* Dry Run Check: If stream_id is INVALID, the host just wants to verify format support. */
    if (stream_id == BLUESPY_ID_INVALID) {
        ret.error = 0;
        ret.format.sample_rate = temp_stream.sample_rate;
        ret.format.n_channels = temp_stream.channels;
        return ret;
    }

    /* Allocate State */
    TEMPLATE_stream* stream = (TEMPLATE_stream*)calloc(1, sizeof(TEMPLATE_stream));
    if (!stream) {
        ret.error = -2;
        return ret;
    }

    /* Apply parsed config */
    stream->parent_stream_id = stream_id;
    stream->sample_rate = temp_stream.sample_rate;
    stream->channels = temp_stream.channels;

    /* Initialize sequence tracking */
    stream->have_seq = false;
    stream->last_seq = 0;
    stream->samples_per_frame = 1024; /* TODO: Update this based on codec spec */

    /* Initialize Decoder */
    /* TODO: Call your actual decoder initialization here */
    stream->decoder_handle = malloc(1);

    if (!stream->decoder_handle) {
        free(stream);
        ret.error = -4;
        return ret;
    }

    stream->initialized = true;

    /* Report success */
    ret.error = 0;
    ret.context_handle = (uintptr_t)stream;

    ret.format.sample_rate = stream->sample_rate;
    ret.format.n_channels = stream->channels;
    ret.format.sample_format = BLUESPY_AUDIO_FORMAT_S16_LE;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;

    return ret;
}

BLUESPY_CODEC_API void codec_decode(uintptr_t context, const uint8_t* payload,
                                    const uint32_t payload_len, bluespy_event_id event_id,
                                    uint64_t sequence_number) {
    TEMPLATE_stream* stream = (TEMPLATE_stream*)context;
    if (!stream || !stream->initialized) {
        return;
    }

    /* -------------------------------------------------------------
     * 1. Gap Detection
     * ------------------------------------------------------------- */
    uint32_t missing_samples = 0;

    /* Determine the sequence number based on transport:
     * - A2DP (AVDTP): Extract from RTP header via `read_be16(payload + RTP_SEQ_OFFSET)`
     * - LE Audio (ISO): Provided directly by host via the `sequence_number` argument.
     */
    uint16_t current_seq = (uint16_t)sequence_number; /* Assuming LE Audio for template */

    if (stream->have_seq) {
        int32_t diff = calculate_rtp_seq_diff(current_seq, stream->last_seq);

        if (diff > 1) {
            uint32_t missing_packets = (uint32_t)(diff - 1);
            missing_samples = missing_packets * stream->samples_per_frame;
        }
    }

    stream->last_seq = current_seq;
    stream->have_seq = true;

    /* Handle empty/missing payloads (Sniffer timeline still needs the gap reported) */
    if (!payload || payload_len == 0) {
        if (missing_samples > 0) {
            bluespy_add_audio(NULL, 0, event_id, missing_samples);
        }
        return;
    }

    /* -------------------------------------------------------------
     * 2. Header Stripping (For A2DP / RTP only)
     * ------------------------------------------------------------- */
    /* uint32_t rtp_len = get_rtp_header_length(payload, payload_len);
    if (rtp_len == 0) return;
    const uint8_t* frame_data = payload + rtp_len;
    uint32_t frame_len = payload_len - rtp_len;
    */

    /* -------------------------------------------------------------
     * 3. Decode Frame
     * ------------------------------------------------------------- */
    /* TODO: Pass payload (or frame_data) to your decoder handle.
     * WARNING: If decode fails due to missing data, DO NOT generate
     * Packet Loss Concealment (PLC). This is a sniffer, so missing
     * data should remain silent to accurately reflect the air trace.
     */
    size_t bytes_to_copy = payload_len;
    if (bytes_to_copy > sizeof(stream->pcm_buffer)) {
        bytes_to_copy = sizeof(stream->pcm_buffer);
    }
    memcpy(stream->pcm_buffer, payload, bytes_to_copy);

    /* -------------------------------------------------------------
     * 4. Deliver PCM
     * ------------------------------------------------------------- */
    bluespy_add_audio((const uint8_t*)stream->pcm_buffer, (uint32_t)bytes_to_copy, event_id,
                      missing_samples);
}

BLUESPY_CODEC_API void codec_deinit(uintptr_t context) {
    TEMPLATE_stream* stream = (TEMPLATE_stream*)context;
    if (stream) {
        /* Free internal decoder resources */
        if (stream->decoder_handle) {
            free(stream->decoder_handle);
        }
        free(stream);
    }
}

} // end extern "C"