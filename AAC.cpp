// Copyright RF Creations Ltd 2026
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file AAC.cpp
 * @brief AAC codec plugin for blueSPY
 */

#include "aacdecoder_lib.h"
#include "bluespy_codec_interface.h"
#include "bluespy_codec_utils.h"
#include "codec_structures.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------------
 * Constants & Bitmasks
 *----------------------------------------------------------------------------*/

#define PCM_BUFFER_SAMPLES 16384 /* Max samples per decode cycle */
#define MIN_AAC_CONFIG_LEN                                                                         \
    6 /* Service_Category(1) + LOSC(1) + Media_Type(1) + Codec_Type(1) + Info(2+) */

/* A2DP AAC Sample Rate Masks (Octet 1 of Media Codec Specific Information) */
#define AAC_SAMP_FREQ_8000 0x80
#define AAC_SAMP_FREQ_11025 0x40
#define AAC_SAMP_FREQ_12000 0x20
#define AAC_SAMP_FREQ_16000 0x10
#define AAC_SAMP_FREQ_22050 0x08
#define AAC_SAMP_FREQ_24000 0x04
#define AAC_SAMP_FREQ_32000 0x02
#define AAC_SAMP_FREQ_44100 0x01

/* A2DP AAC Sample Rate Masks (Octet 2 of Media Codec Specific Information) */
#define AAC_SAMP_FREQ_48000 0x80
#define AAC_SAMP_FREQ_64000 0x40
#define AAC_SAMP_FREQ_88200 0x20
#define AAC_SAMP_FREQ_96000 0x10

/* A2DP AAC Channel Mode Masks (Octet 2 of Media Codec Specific Information) */
#define AAC_CH_MODE_1 0x08
#define AAC_CH_MODE_2 0x04

/*------------------------------------------------------------------------------
 * Types
 *----------------------------------------------------------------------------*/

/**
 * @brief AAC decoder state
 */
typedef struct {
    bluespy_audiostream_id parent_stream_id;
    bool initialized;

    /* RTP Sequence Tracking */
    bool has_last_seq;
    uint16_t last_rtp_seq;

    /* Gap Calculation Helpers */
    int frames_per_packet;
    uint32_t samples_per_frame;

    HANDLE_AACDECODER decoder;
    uint32_t sample_rate;
    uint8_t channels;

    /* Decoded PCM Buffer */
    int16_t pcm_buffer[PCM_BUFFER_SAMPLES];
} AAC_stream;

/*------------------------------------------------------------------------------
 * Helper Functions
 *----------------------------------------------------------------------------*/

/**
 * @brief Parse sample rate from AAC Media Codec Specific Information
 *
 * @param cfg  Pointer to Media_Codec_Specific_Information
 * @return Sample rate in Hz, or 0 if not recognized
 */
static uint32_t parse_sample_rate(const uint8_t* cfg) {
    uint8_t byte1 = cfg[1];
    uint8_t byte2 = cfg[2];

    if (byte1 & AAC_SAMP_FREQ_44100)
        return 44100;
    if (byte2 & AAC_SAMP_FREQ_48000)
        return 48000;
    if (byte1 & AAC_SAMP_FREQ_32000)
        return 32000;
    if (byte1 & AAC_SAMP_FREQ_24000)
        return 24000;
    if (byte1 & AAC_SAMP_FREQ_22050)
        return 22050;
    if (byte1 & AAC_SAMP_FREQ_16000)
        return 16000;
    if (byte1 & AAC_SAMP_FREQ_12000)
        return 12000;
    if (byte1 & AAC_SAMP_FREQ_11025)
        return 11025;
    if (byte1 & AAC_SAMP_FREQ_8000)
        return 8000;

    if (byte2 & AAC_SAMP_FREQ_64000)
        return 64000;
    if (byte2 & AAC_SAMP_FREQ_88200)
        return 88200;
    if (byte2 & AAC_SAMP_FREQ_96000)
        return 96000;

    return 0;
}

/**
 * @brief Parse channel count from AAC Media Codec Specific Information
 *
 * @param cfg Pointer to Media_Codec_Specific_Information
 * @return Number of channels (1 or 2)
 */
static uint8_t parse_channels(const uint8_t* cfg) { return (cfg[2] & AAC_CH_MODE_1) ? 1 : 2; }

/*------------------------------------------------------------------------------
 * API Implementation
 *----------------------------------------------------------------------------*/

extern "C" {

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void) {
    return bluespy_audio_codec_lib_info{.api_version = BLUESPY_AUDIO_API_VERSION,
                                        .codec_name = "AAC"};
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret
new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret ret = {
        .error = -1, .format = {0}, .fns = {0}, .context_handle = 0};

    if (!info || info->container != BLUESPY_CODEC_AVDTP) {
        return ret;
    }

    /* Validate configuration */
    const AVDTP_Service_Capabilities_Media_Codec_t* cap =
        (const AVDTP_Service_Capabilities_Media_Codec_t*)info->config;
    if (!cap || cap->Media_Codec_Type != AVDTP_Codec_MPEG_24_AAC) {
        return ret;
    }
    if (info->config_len < MIN_AAC_CONFIG_LEN) {
        ret.error = -2;
        return ret;
    }

    /* Parse codec configuration */
    const uint8_t* codec_info = cap->Media_Codec_Specific_Information;
    uint32_t sample_rate = parse_sample_rate(codec_info);
    if (sample_rate == 0) {
        ret.error = -4;
        return ret;
    }
    uint8_t channels = parse_channels(codec_info);
    bluespy_channel_mode channel_mode =
        (channels == 1) ? BLUESPY_CH_MODE_MONO : BLUESPY_CH_MODE_STEREO;

    /* Dry run to allow the host to check if this codec format is supported */
    if (stream_id == BLUESPY_ID_INVALID) {
        ret.error = 0;
        ret.format.sample_rate = sample_rate;
        ret.format.n_channels = channels;
        ret.format.channel_mode = channel_mode;

        return ret;
    }

    /* Allocate State */
    AAC_stream* stream = (AAC_stream*)calloc(1, sizeof(AAC_stream));
    if (!stream) {
        ret.error = -3;
        return ret;
    }

    /* Create FDK-AAC decoder */
    stream->parent_stream_id = stream_id;
    stream->decoder = aacDecoder_Open(TT_MP4_LATM_MCP1, 1);
    if (!stream->decoder) {
        free(stream);
        ret.error = -5;
        return ret;
    }

    aacDecoder_SetParam(stream->decoder, AAC_PCM_MIN_OUTPUT_CHANNELS, channels);
    aacDecoder_SetParam(stream->decoder, AAC_PCM_MAX_OUTPUT_CHANNELS, channels);

    /* Store configuration */
    stream->sample_rate = sample_rate;
    stream->channels = channels;
    stream->initialized = true;
    stream->has_last_seq = false;
    stream->last_rtp_seq = 0;
    stream->frames_per_packet = 1;    // Default to 1, updated dynamically
    stream->samples_per_frame = 1024; // Standard AAC frame size, updated on decode

    /* Success */
    ret.error = 0;
    ret.context_handle = (uintptr_t)stream;

    ret.format.sample_rate = sample_rate;
    ret.format.n_channels = channels;
    ret.format.sample_format = BLUESPY_AUDIO_FORMAT_S16_LE;
    ret.format.channel_mode = channel_mode;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;

    return ret;
}

BLUESPY_CODEC_API void codec_decode(uintptr_t context, const uint8_t* payload, uint32_t payload_len,
                                    bluespy_event_id event_id, uint64_t sequence_number) {
    AAC_stream* stream = (AAC_stream*)context;

    if (!stream || !stream->initialized || !stream->decoder) {
        return;
    }

    uint32_t rtp_header_len = get_rtp_header_length(payload, payload_len);
    if (rtp_header_len == 0) {
        return;
    }

    /* Extract RTP Sequence Number and calculate missing samples for the sniffer timeline */
    uint16_t rtp_seq = read_be16(payload + RTP_SEQ_OFFSET);
    uint32_t missing_samples = 0;

    if (stream->has_last_seq) {
        int32_t diff = calculate_rtp_seq_diff(rtp_seq, stream->last_rtp_seq);

        if (diff <= 0) {
            return; // Duplicate or wildly out-of-order packet
        }

        if (diff > 1) {
            uint32_t missing_packets = (uint32_t)(diff - 1);
            missing_samples =
                missing_packets * stream->frames_per_packet * stream->samples_per_frame;
        }
    }

    stream->last_rtp_seq = rtp_seq;
    stream->has_last_seq = true;

    /* -------------------------------------------------------------
     * Normal Decoding
     * ------------------------------------------------------------- */
    const uint8_t* aac_data = payload + rtp_header_len;
    uint32_t aac_data_len = payload_len - rtp_header_len;

    size_t total_samples = 0;
    int frames_in_this_packet = 0;

    while (aac_data_len > 0) {
        UINT bytes_valid = aac_data_len;
        const UCHAR* in_ptr = aac_data;

        AAC_DECODER_ERROR err =
            aacDecoder_Fill(stream->decoder, (UCHAR**)&in_ptr, &aac_data_len, &bytes_valid);
        if (err != AAC_DEC_OK) {
            break;
        }

        err = aacDecoder_DecodeFrame(stream->decoder, stream->pcm_buffer + total_samples,
                                     PCM_BUFFER_SAMPLES - total_samples, 0);

        /* Handle fragmentation or other decode errors */
        if (err == AAC_DEC_NOT_ENOUGH_BITS || err != AAC_DEC_OK) {
            break;
        }

        CStreamInfo* frame_info = aacDecoder_GetStreamInfo(stream->decoder);
        if (frame_info) {
            total_samples += frame_info->frameSize * frame_info->numChannels;
            frames_in_this_packet++;
            stream->samples_per_frame = frame_info->frameSize;
        }

        aac_data_len = bytes_valid;
    }

    /* Update heuristic for gap calculation */
    if (frames_in_this_packet > 0) {
        stream->frames_per_packet = frames_in_this_packet;
    }

    if (total_samples > 0) {
        bluespy_add_audio((const uint8_t*)stream->pcm_buffer, total_samples * sizeof(int16_t),
                          event_id, missing_samples);
    } else if (missing_samples > 0) {
        bluespy_add_audio(NULL, 0, event_id, missing_samples);
    }
}

BLUESPY_CODEC_API void codec_deinit(uintptr_t context) {
    AAC_stream* stream = (AAC_stream*)context;
    if (stream) {
        if (stream->decoder) {
            aacDecoder_Close(stream->decoder);
        }
        free(stream);
    }
}

} // end extern "C"