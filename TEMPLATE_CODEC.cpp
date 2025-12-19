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

/** Maximum number of concurrent audio streams handled by this codec - must be at least 2 */
#define MAX_STREAMS 16

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
    bool in_use;
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
 * Static Storage
 *----------------------------------------------------------------------------*/

static TEMPLATE_stream g_streams[MAX_STREAMS];

/*------------------------------------------------------------------------------
 * Helper Functions
 *----------------------------------------------------------------------------*/

/**
 * @brief Find a decoder stream by its ID.
 */
static TEMPLATE_stream* stream_find(bluespy_audiostream_id id)
{
    for (int i = 0; i < MAX_STREAMS; ++i) {
        if (g_streams[i].in_use && g_streams[i].stream_id == id) {
            return &g_streams[i];
        }
    }
    return NULL;
}

/**
 * @brief Allocate a new decoder stream slot.
 */
static TEMPLATE_stream* stream_allocate(bluespy_audiostream_id id)
{
    TEMPLATE_stream* existing = stream_find(id);
    if (existing) {
        return existing;
    }

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
 * @brief Release all resources associated with a stream.
 */
static void stream_release(TEMPLATE_stream* stream)
{
    if (!stream || !stream->in_use) {
        return;
    }

    /* Free or close your codec decoder handle here */
    free(stream->decoder_handle);

    memset(stream, 0, sizeof(*stream));
}

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
        .codec_name = "TEMPLATE_CODEC"
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
        .fns = {0}
    };

    /* Parameter validation */
    if (!info || !info->config || info->config_len == 0) {
        return ret;
    }

    /* Allocate new stream */
    TEMPLATE_stream* stream = stream_allocate(stream_id);
    if (!stream) {
        ret.error = -2;
        return ret;
    }

    /* Parse configuration from container */
    if (!parse_codec_config(info, stream)) {
        stream_release(stream);
        ret.error = -3;
        return ret;
    }

    /* Create codec handle (stub) */
    stream->decoder_handle = malloc(1); /* Replace with actual decoder init */
    if (!stream->decoder_handle) {
        stream_release(stream);
        ret.error = -4;
        return ret;
    }

    stream->initialized = true;

    /* Report success */
    ret.error = 0;
    ret.format.sample_rate = stream->sample_rate;
    ret.format.n_channels = stream->channels;
    ret.format.bits_per_sample = 16;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;

    return ret;
}

/**
 * @brief Decode a codec frame or SDU and deliver PCM samples to the host.
 *
 * For A2DP, the payload usually contains an RTP header + codec frames.
 * For LE Audio, each payload represents one ISOAL SDU (possibly multiple frames).
 *
 * Call `bluespy_add_decoded_audio()` to deliver the PCM output to blueSPY.
 */
BLUESPY_CODEC_API void codec_decode(bluespy_audiostream_id stream_id, const uint8_t* payload, const uint32_t payload_len, bluespy_event_id event_id, uint64_t sequence_number)
{
    (void)sequence_number;

    TEMPLATE_stream* stream = stream_find(stream_id);
    if (!stream || !stream->initialized || !payload || payload_len == 0) {
        return;
    }

    /* ---- Replace this with your codec's decode logic ----
     * Example placeholder: simply copy payload into PCM buffer.
     */
    size_t bytes_to_copy = payload_len;
    if (bytes_to_copy > sizeof(stream->pcm_buffer)) {
        bytes_to_copy = sizeof(stream->pcm_buffer);
    }

    memcpy(stream->pcm_buffer, payload, bytes_to_copy);

    /* Deliver decoded PCM (16-bit little-endian) */
    bluespy_add_decoded_audio(
        (const uint8_t*)stream->pcm_buffer,
        (uint32_t)bytes_to_copy,
        event_id);
}

/**
 * @brief Deinitialize a codec stream and release resources.
 *
 * This function must tolerate redundant calls for the same stream ID.
 */
BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id stream_id)
{
    TEMPLATE_stream* stream = stream_find(stream_id);
    if (stream) {
        stream_release(stream);
    }
}

} // end extern "C"