/**
 * @file TEMPLATE_CODEC.cpp
 * @brief Template codec plugin for blueSPY
 *
 * Use this file as a skeleton for implementing a new codec plugin.
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
#include "codec_structures.h"

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

/*------------------------------------------------------------------------------
 * Configuration Constants (adjust as needed)
 *----------------------------------------------------------------------------*/

/** Example PCM buffer size (in S16 samples) */
#define TEMPLATE_PCM_BUFFER_SAMPLES 8192

/*------------------------------------------------------------------------------
 * Data Structures
 *----------------------------------------------------------------------------*/

/**
 * @brief Per-stream codec decoder state.
 *
 * Each active stream has one instance of this structure.  Put any
 * codec-specific decoder handles, context state, or buffers here.
 */
typedef struct {
    bluespy_audiostream_id stream_id;
    bool initialized;

    /* Example configuration */
    uint32_t sample_rate;
    uint8_t  channels;

    /* Example codec-specific handle (replace with real type) */
    void* decoder_handle;

    /* PCM output buffer (16-bit samples, interleaved) */
    int16_t pcm_buffer[TEMPLATE_PCM_BUFFER_SAMPLES];

} TEMPLATE_stream;

/*------------------------------------------------------------------------------
 * Codec Configuration Parsing (optional, example stub)
 *----------------------------------------------------------------------------*/

/**
 * @brief Parse configuration data from blueSPY container (see codec_structures.h).
 *
 * The structure and meaning of the config block depend on the container type:
 *   - BLUESPY_CODEC_AVDTP: use AVDTP_Service_Capabilities_Media_Codec_t
 *   - BLUESPY_CODEC_CIS:   use LEA_Codec_Specific_Config_t
 *   - BLUESPY_CODEC_BIS:   use LEA_Broadcast_Codec_Config_t
 *
 * You may safely assume `config` points to a valid container block
 * of `config_len` bytes (host side guarantees bounds).
 */
static bool parse_codec_config(const bluespy_audio_codec_info* info, TEMPLATE_stream* stream)
{
    if (!info || !info->config || info->config_len == 0) {
        return false;
    }

    switch (info->container) {
        case BLUESPY_CODEC_AVDTP:
            /* Typical for Classic A2DP codecs — parse AVDTP capabilities here */
            stream->sample_rate = 44100;
            stream->channels = 2;
            break;

        case BLUESPY_CODEC_CIS:
            /* LE Audio (Connected Isochronous Stream) configuration */
            stream->sample_rate = 48000;
            stream->channels = 1;
            break;

        case BLUESPY_CODEC_BIS:
            /* LE Audio Broadcast Isochronous Stream configuration */
            stream->sample_rate = 48000;
            stream->channels = 2;
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

/**
 * @brief Library-level initialization.
 *
 * Called once when blueSPY loads this codec plugin.
 * Must return the API version and human-readable name.
 */
BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void)
{
    return (bluespy_audio_codec_lib_info){
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name = "TEMPLATE_CODEC" /* Change this name */
    };
}

/**
 * @brief Create and initialize a new codec stream.
 *
 * blueSPY calls this whenever a new captured audio stream starts.
 * Implementations should:
 *   - Parse the codec configuration from `info`.
 *   - Allocate and initialize decoder resources.
 *   - Return decoded format info and function pointers.
 */
BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret ret = {
        .error = -1,
        .format = {0},
        .fns = {0},
        .context_handle = 0
    };

    /* Parameter validation */
    if (!info || !info->config || info->config_len == 0) {
        return ret;
    }

    /* Add a check here to ensure this plugin actually supports 
     * the requested codec (e.g. check Vendor ID in info->config).
     * If not supported, return error = -1.
    */

    /* Dry Run Check */
    /* If stream_id is INVALID, the host just wants to verify support. */
    if (stream_id == BLUESPY_ID_INVALID) {
        ret.error = 0;
        return ret;
    }

    /* Allocate State */
    TEMPLATE_stream* stream = (TEMPLATE_stream*)calloc(1, sizeof(TEMPLATE_stream));
    if (!stream) {
        ret.error = -2;
        return ret;
    }

    stream->parent_stream_id = stream_id;

    /* Parse configuration from container */
    if (!parse_codec_config(info, stream)) {
        free(stream);
        ret.error = -3;
        return ret;
    }

    /* Initialize Decoder */
    /* Call your actual decoder initialization here */
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

/**
 * @brief Decode a codec frame or SDU and deliver PCM samples to the host.
 *
 * For A2DP, the payload usually contains an RTP header + codec frames.
 * For LE Audio, each payload represents one ISOAL SDU (possibly multiple frames).
 */
BLUESPY_CODEC_API void codec_decode(uintptr_t context, const uint8_t* payload, const uint32_t payload_len, bluespy_event_id event_id, uint64_t sequence_number)
{
    (void)sequence_number;

    TEMPLATE_stream* stream = (TEMPLATE_stream*)context;
    if (!stream || !stream->initialized || !payload || payload_len == 0) {
        return;
    }

    /* Implement Decode Logic
     * 1. Check for RTP headers (if AVDTP) and strip them.
     * 2. Pass payload to your decoder handle.
     * 3. Write output to stream->pcm_buffer.
    */
    size_t bytes_to_copy = payload_len;
    if (bytes_to_copy > sizeof(stream->pcm_buffer)) {
        bytes_to_copy = sizeof(stream->pcm_buffer);
    }

    memcpy(stream->pcm_buffer, payload, bytes_to_copy);

    /* Deliver decoded PCM (16-bit little-endian) */
    bluespy_add_audio(
        (const uint8_t*)stream->pcm_buffer,
        (uint32_t)bytes_to_copy,
        event_id,
        0 /* missing_samples */
    );
}

/**
 * @brief Deinitialize a codec stream and release resources.
 */
BLUESPY_CODEC_API void codec_deinit(uintptr_t context)
{
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