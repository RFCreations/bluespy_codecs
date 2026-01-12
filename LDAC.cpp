// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file LDAC.cpp
 * @brief LDAC codec plugin for blueSPY
 *
 * Implements LDAC decoding for AVDTP/A2DP Classic audio streams
 * using the libldacdec decoder library. LDAC is Sony's high-resolution audio
 * codec supporting up to 96kHz/24bit audio.
 */

#include "bluespy_codec_interface.h"
#include "codec_structures.h"
#include "ldacdec.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------------
 * Constants
 *----------------------------------------------------------------------------*/

#define PCM_BUFFER_SAMPLES      8192    /* Max 16-bit samples per decode cycle */
#define RTP_HEADER_SIZE         12      /* Fixed RTP header size (excludes CSRC) */
#define MIN_PAYLOAD_SIZE        20      /* Minimum valid LDAC packet size */

/** Sony Vendor ID (little-endian) */
#define VENDOR_ID_SONY          0x0000012D

/** LDAC Codec ID */
#define CODEC_ID_LDAC           0xAA

/** LDAC sync byte */
#define LDAC_SYNC_BYTE          0xAA

/** LDAC sample rate bits (in config byte 0, bits 5-0) */
#define LDAC_FREQ_96000         0x20
#define LDAC_FREQ_88200         0x10
#define LDAC_FREQ_48000         0x08
#define LDAC_FREQ_44100         0x04

/** LDAC channel mode (in config byte 0, bits 7-6) */
typedef enum {
    LDAC_CH_MODE_STEREO     = 0,    /* Stereo */
    LDAC_CH_MODE_DUAL       = 1,    /* Dual channel */
    LDAC_CH_MODE_MONO       = 2     /* Mono */
} LDAC_channel_mode;

/*------------------------------------------------------------------------------
 * Types
 *----------------------------------------------------------------------------*/

/**
 * @brief LDAC decoder state
 */
typedef struct {
    bluespy_audiostream_id parent_stream_id;
    bool initialized;

    /* RTP sequence tracking */
    bool has_last_seq;
    uint16_t last_rtp_seq;

    /* Gap estimation heuristic */
    uint32_t samples_per_packet;

    /* ldacBT decoder instance */
    ldacdec_t decoder;

    /* Stream configuration (may be updated during decode) */
    uint32_t sample_rate;
    uint8_t  channels;

    /* Output buffer */
    int16_t pcm_buffer[PCM_BUFFER_SAMPLES];
} LDAC_stream;

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
 * @brief Check if configuration is for LDAC codec
 *
 * @param cap  AVDTP Media Codec capability structure
 * @return true if this is an LDAC configuration
 */
static bool is_ldac_config(const AVDTP_Service_Capabilities_Media_Codec_t* cap)
{
    if (cap->Media_Codec_Type != AVDTP_Codec_Vendor_Specific) {
        return false;
    }

    const uint8_t* info = cap->Media_Codec_Specific_Information;
    uint32_t vendor_id = read_le32(info);
    uint8_t codec_id = info[4];

    return (vendor_id == VENDOR_ID_SONY && codec_id == CODEC_ID_LDAC);
}

/**
 * @brief Parse sample rate from LDAC configuration
 *
 * @param config  Pointer to Media_Codec_Specific_Information
 * @return Sample rate in Hz
 */
static uint32_t parse_sample_rate(const uint8_t* config)
{
    uint8_t freq_bits = config[0] & 0x3F;

    if (freq_bits & LDAC_FREQ_96000) return 96000;
    if (freq_bits & LDAC_FREQ_88200) return 88200;
    if (freq_bits & LDAC_FREQ_48000) return 48000;
    if (freq_bits & LDAC_FREQ_44100) return 44100;

    /* Default to 48kHz */
    return 48000;
}

/**
 * @brief Parse channel count from LDAC configuration
 *
 * @param config  Pointer to Media_Codec_Specific_Information
 * @return Number of channels (1 or 2)
 */
static uint8_t parse_channels(const uint8_t* config)
{
    uint8_t ch_mode = (config[0] >> 6) & 0x03;

    switch (ch_mode) {
        case LDAC_CH_MODE_MONO:
            return 1;
        case LDAC_CH_MODE_STEREO:
        case LDAC_CH_MODE_DUAL:
        default:
            return 2;
    }
}

/*------------------------------------------------------------------------------
 * RTP / Frame Processing
 *----------------------------------------------------------------------------*/

/**
 * @brief Calculate RTP header length including CSRC fields
 *
 * @param payload     Pointer to RTP packet
 * @param payload_len Total payload length
 * @return Header length in bytes, or 0 if invalid
 */
static uint32_t get_rtp_header_length(const uint8_t* payload, uint32_t payload_len)
{
    if (payload_len < RTP_HEADER_SIZE) {
        return 0;
    }

    uint32_t csrc_count = payload[0] & 0x0F;
    uint32_t header_len = RTP_HEADER_SIZE + (4 * csrc_count);

    if (header_len >= payload_len) {
        return 0;
    }

    return header_len;
}

/**
 * @brief Find LDAC sync byte in buffer
 *
 * @param data      Buffer to search
 * @param length    Length of buffer
 * @return Offset to sync byte, or length if not found
 */
static uint32_t find_sync_byte(const uint8_t* data, uint32_t length)
{
    for (uint32_t i = 0; i < length; ++i) {
        if (data[i] == LDAC_SYNC_BYTE) {
            return i;
        }
    }
    return length;
}

/*------------------------------------------------------------------------------
 * API Implementation
 *----------------------------------------------------------------------------*/

extern "C" {

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void)
{
    return bluespy_audio_codec_lib_info{
        .api_version = BLUESPY_AUDIO_API_VERSION,
        .codec_name = "LDAC"
    };
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info)
{
    bluespy_audio_codec_init_ret ret = {
        .error = -1,
        .format = {0},
        .fns = {0},
        .context_handle = 0
    };

    /* Only handle AVDTP container */
    if (!info || info->container != BLUESPY_CODEC_AVDTP) {
        return ret;
    }

    /* Validate configuration */
    const AVDTP_Service_Capabilities_Media_Codec_t* cap = (const AVDTP_Service_Capabilities_Media_Codec_t*)info->config;
    if (!cap || !is_ldac_config(cap)) {
        return ret;
    }
    if (info->config_len < 6) {
        ret.error = -2;
        return ret;
    }

    /* Dry run to allow the host to check if this codec format is supported */
    if (stream_id == BLUESPY_ID_INVALID) {
        ret.error = 0;
        return ret;
    }
    
    /* Allocate State */
    LDAC_stream* stream = (LDAC_stream*)calloc(1, sizeof(LDAC_stream));
    if (!stream) {
        ret.error = -3;
        return ret;
    }
    /* Parse configuration */
    const uint8_t* codec_info = cap->Media_Codec_Specific_Information;
    stream->sample_rate = parse_sample_rate(codec_info);
    stream->channels = parse_channels(codec_info);
    stream->parent_stream_id = stream_id;

    /* Initialise LDAC decoder */
    memset(&stream->decoder, 0, sizeof(stream->decoder));
    if (ldacdecInit(&stream->decoder) < 0) {
        free(stream);
        ret.error = -4;
        return ret;
    }

    stream->initialized = true;
    stream->has_last_seq = false;
    stream->last_rtp_seq = 0;
    stream->samples_per_packet = 128 * stream->channels; // conservative initial guess

    /* Success */
    ret.error = 0;
    ret.context_handle = (uintptr_t)stream;

    ret.format.sample_rate = stream->sample_rate;
    ret.format.n_channels = stream->channels;
    ret.format.sample_format = BLUESPY_AUDIO_FORMAT_S16_LE;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;

    return ret;
}

BLUESPY_CODEC_API void codec_decode(uintptr_t context, const uint8_t* payload, uint32_t payload_len, bluespy_event_id event_id, uint64_t sequence_number)
{
    (void)sequence_number;

    LDAC_stream* stream = (LDAC_stream*)context;
    if (!stream || !stream->initialized) {
        return;
    }

    if (!payload || payload_len < MIN_PAYLOAD_SIZE) {
        return;
    }

    /* Extract RTP sequence number and calculate gap */
    uint16_t rtp_seq = (uint16_t)(payload[2] << 8) | payload[3];
    uint32_t missing_samples = 0;

    if (stream->has_last_seq) {
        int32_t diff = (int32_t)rtp_seq - (int32_t)stream->last_rtp_seq;

        if (diff < -32768) {
            diff += 65536;
        } else if (diff > 32768) {
            diff -= 65536;
        }

        if (diff > 1) {
            // Gap detected
            uint32_t missing_packets = (uint32_t)(diff - 1);
            missing_samples = missing_packets * stream->samples_per_packet;
        }
    }

    stream->last_rtp_seq = rtp_seq;
    stream->has_last_seq = true;

    /* Strip RTP header */
    uint32_t rtp_len = get_rtp_header_length(payload, payload_len);
    if (rtp_len == 0) {
        return;
    }

    const uint8_t* frame = payload + rtp_len;
    uint32_t remaining = payload_len - rtp_len;

    /* Find first LDAC sync byte (0xAA) */
    uint32_t sync_offset = find_sync_byte(frame, remaining);
    if (sync_offset >= remaining) {
        return;
    }

    frame += sync_offset;
    remaining -= sync_offset;

    /* Decode LDAC frames */
    int16_t* pcm_out = stream->pcm_buffer;
    size_t total_samples = 0;
    const size_t max_samples = PCM_BUFFER_SAMPLES;

    while (remaining > 0 && total_samples < max_samples) {
        int bytes_consumed = 0;
        int result = ldacDecode(&stream->decoder, 
                                (uint8_t*)frame, 
                                pcm_out + total_samples, 
                                &bytes_consumed);

        if (result < 0) {
            /* Decode error - attempt to resync */
            uint32_t resync_offset = find_sync_byte(frame + 1, remaining - 1);
            if (resync_offset >= remaining - 1) {
                break;  /* No more sync bytes found */
            }
            frame += 1 + resync_offset;
            remaining -= 1 + resync_offset;
            continue;
        }

        if (bytes_consumed <= 0) {
            break;
        }

        if ((uint32_t)bytes_consumed > remaining) {
            break;
        }

        frame += bytes_consumed;
        remaining -= bytes_consumed;

        /* Get frame info from decoder */
        int frame_samples = stream->decoder.frame.frameSamples;
        int frame_channels = stream->decoder.frame.channelCount;
        size_t samples_decoded = (size_t)(frame_samples * frame_channels);

        total_samples += samples_decoded;
    }

    if (total_samples > 0) {
        stream->samples_per_packet = (uint32_t)total_samples / stream->channels;
        
        /* Update stream parameters from decoder (may change during stream) */
        stream->sample_rate = ldacdecGetSampleRate(&stream->decoder);
        stream->channels = (uint8_t)ldacdecGetChannelCount(&stream->decoder);

        /* Deliver decoded audio */
        uint32_t pcm_bytes = (uint32_t)(total_samples * sizeof(int16_t));
        bluespy_add_audio((const uint8_t*)stream->pcm_buffer, pcm_bytes, event_id, missing_samples);
    } else if (missing_samples > 0) {
        bluespy_add_audio(NULL, 0, event_id, missing_samples);
    }
}

BLUESPY_CODEC_API void codec_deinit(uintptr_t context)
{
    LDAC_stream* stream = (LDAC_stream*)context;
    if (stream) {
        free(stream);
    }
}

} // end extern "C"