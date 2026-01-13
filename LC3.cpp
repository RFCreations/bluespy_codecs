// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file LC3.cpp
 * @brief LC3 codec plugin for blueSPY
 *
 * Implements LC3 decoding for both CIS (Connected Isochronous Stream) and
 * BIS (Broadcast Isochronous Stream) LE Audio containers.
 *
 * NOTE: blueSPY will natively decode LC3 streams without this plugin. This file
 *       is designed only to serve as an example of how the API works for LE Audio codecs.
 */

#include "bluespy_codec_interface.h"
#include "codec_structures.h"
#include <lc3.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*------------------------------------------------------------------------------
 * Constants
 *----------------------------------------------------------------------------*/

#define MAX_CHANNELS 8

/** BASE Service UUID for Basic Audio Announcement (0x1851) */
#define UUID_BASIC_AUDIO_ANNOUNCEMENT 0x1851

/** AD Type codes */
#define AD_TYPE_SERVICE_DATA 0x16
#define AD_TYPE_BIG_INFO 0x2C

/** LC3 Codec Specific Configuration LTV Type codes (Assigned Numbers, Section 6.12.4) */
typedef enum {
    LTV_TYPE_SAMPLING_FREQ = 0x01,
    LTV_TYPE_FRAME_DURATION = 0x02,
    LTV_TYPE_AUDIO_CHANNEL_ALLOC = 0x03,
    LTV_TYPE_OCTETS_PER_FRAME = 0x04,
    LTV_TYPE_FRAME_BLOCKS_PER_SDU = 0x05
} LC3_LTV_type;

/** LC3 Sampling Frequency codes (Assigned Numbers, Section 6.12.4.1) */
typedef enum {
    LC3_FREQ_8000 = 0x01,
    LC3_FREQ_11025 = 0x02,
    LC3_FREQ_16000 = 0x03,
    LC3_FREQ_22050 = 0x04,
    LC3_FREQ_24000 = 0x05,
    LC3_FREQ_32000 = 0x06,
    LC3_FREQ_44100 = 0x07,
    LC3_FREQ_48000 = 0x08
} LC3_sampling_freq_code;

/** LC3 Frame Duration codes (Assigned Numbers, Section 6.12.4.2) */
typedef enum { LC3_DUR_7500US = 0x00, LC3_DUR_10000US = 0x01 } LC3_frame_duration_code;

/** Default configuration values */
#define DEFAULT_SAMPLE_RATE_HZ 48000
#define DEFAULT_FRAME_DURATION_US 10000
#define DEFAULT_CHANNELS 1
#define DEFAULT_OCTETS_PER_FRAME 100

/*------------------------------------------------------------------------------
 * Types
 *----------------------------------------------------------------------------*/

/**
 * @brief Parsed LC3 codec configuration
 */
typedef struct {
    uint32_t sample_rate_hz;
    uint32_t frame_duration_us;
    uint16_t octets_per_frame;
    uint8_t channels;
} LC3_config;

/**
 * @brief Per-stream decoder state
 */
typedef struct {
    bluespy_audiostream_id parent_stream_id;
    bool initialized;

    /* Configuration */
    LC3_config config;
    size_t samples_per_frame;

    /* Decoder instances (one per channel) */
    void* decoder_mem[MAX_CHANNELS];
    lc3_decoder_t decoder[MAX_CHANNELS];

    /* Output buffer (interleaved S16 PCM) */
    int16_t* pcm_buffer;
    size_t pcm_buffer_bytes;

    /* Sequence tracking */
    uint64_t last_seq;
    bool have_seq;
} LC3_stream;

/*------------------------------------------------------------------------------
 * Utility Functions
 *----------------------------------------------------------------------------*/

/**
 * @brief Count number of set bits in a little-endian byte array
 *
 * Used to count audio channels from the Audio_Channel_Allocation bitmask.
 */
static uint8_t popcount_bytes(const uint8_t* data, size_t len) {
    uint32_t mask = 0;

    for (size_t i = 0; i < len && i < sizeof(mask); ++i) {
        mask |= (uint32_t)data[i] << (i * 8);
    }

#if defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_popcount(mask);
#elif defined(_MSC_VER)
    return (uint8_t)__popcnt(mask);
#else
    uint8_t count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
#endif
}

/**
 * @brief Convert LC3 sampling frequency code to Hz
 */
static uint32_t freq_code_to_hz(uint8_t code) {
    switch (code) {
    case LC3_FREQ_8000:
        return 8000;
    case LC3_FREQ_11025:
        return 11025;
    case LC3_FREQ_16000:
        return 16000;
    case LC3_FREQ_22050:
        return 22050;
    case LC3_FREQ_24000:
        return 24000;
    case LC3_FREQ_32000:
        return 32000;
    case LC3_FREQ_44100:
        return 44100;
    case LC3_FREQ_48000:
        return 48000;
    default:
        return DEFAULT_SAMPLE_RATE_HZ;
    }
}

/**
 * @brief Convert LC3 frame duration code to microseconds
 */
static inline uint32_t duration_code_to_us(uint8_t code) {
    return (code == LC3_DUR_10000US) ? 10000 : 7500;
}

/**
 * @brief Read little-endian uint16 from buffer
 */
static inline uint16_t read_le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/*------------------------------------------------------------------------------
 * Resource Management
 *----------------------------------------------------------------------------*/

static void stream_free_resources(LC3_stream* stream) {
    if (!stream)
        return;

    for (int i = 0; i < MAX_CHANNELS; ++i) {
        if (stream->decoder_mem[i]) {
            free(stream->decoder_mem[i]);
            stream->decoder_mem[i] = NULL;
        }
    }
    if (stream->pcm_buffer) {
        free(stream->pcm_buffer);
        stream->pcm_buffer = NULL;
    }
}

/*------------------------------------------------------------------------------
 * Configuration Parsing
 *----------------------------------------------------------------------------*/

/**
 * @brief Initialise config with default values
 */
static void config_set_defaults(LC3_config* cfg) {
    cfg->sample_rate_hz = DEFAULT_SAMPLE_RATE_HZ;
    cfg->frame_duration_us = DEFAULT_FRAME_DURATION_US;
    cfg->octets_per_frame = DEFAULT_OCTETS_PER_FRAME;
    cfg->channels = DEFAULT_CHANNELS;
}

/**
 * @brief Parse LTV-encoded codec configuration
 *
 * @param cfg       Output configuration structure
 * @param ltv       Pointer to LTV data
 * @param ltv_len   Length of LTV data in bytes
 */
static void parse_ltv_config(LC3_config* cfg, const uint8_t* ltv, size_t ltv_len) {
    const uint8_t* p = ltv;
    const uint8_t* end = ltv + ltv_len;

    config_set_defaults(cfg);

    while (p + 2 <= end) {
        uint8_t len = p[0];
        uint8_t type = p[1];

        /* Validate LTV bounds */
        if (len == 0 || p + 1 + len > end) {
            break;
        }

        const uint8_t* value = p + 2;
        uint8_t value_len = len - 1;

        switch (type) {
        case LTV_TYPE_SAMPLING_FREQ:
            if (value_len >= 1) {
                cfg->sample_rate_hz = freq_code_to_hz(value[0]);
            }
            break;

        case LTV_TYPE_FRAME_DURATION:
            if (value_len >= 1) {
                cfg->frame_duration_us = duration_code_to_us(value[0]);
            }
            break;

        case LTV_TYPE_AUDIO_CHANNEL_ALLOC:
            if (value_len >= 1) {
                uint8_t ch = popcount_bytes(value, value_len);
                cfg->channels = (ch > 0) ? ch : DEFAULT_CHANNELS;
            }
            break;

        case LTV_TYPE_OCTETS_PER_FRAME:
            if (value_len >= 2) {
                cfg->octets_per_frame = read_le16(value);
            } else if (value_len == 1) {
                cfg->octets_per_frame = value[0];
            }
            break;

        default:
            /* Ignore unknown types for forward compatibility */
            break;
        }

        p += 1 + len;
    }
}

/**
 * @brief Extract LTV pointer and length from CIS configuration
 *
 * CIS configuration format:
 *   - 5 bytes: Codec_ID
 *   - 1 byte:  Codec_Specific_Configuration_Length
 *   - N bytes: Codec_Specific_Configuration (LTVs)
 */
static bool parse_cis_container(const void* config, uint32_t config_len, const uint8_t** ltv_out,
                                size_t* ltv_len_out) {
    if (config_len < 7) {
        return false;
    }

    const LEA_Codec_Specific_Config_t* cis = (const LEA_Codec_Specific_Config_t*)config;
    const uint8_t* ltv_start = cis->Codec_Specific_Information;
    size_t header_size = (size_t)(ltv_start - (const uint8_t*)config);
    size_t available = config_len - header_size;
    size_t ltv_len = (cis->Cap_Length <= available) ? cis->Cap_Length : available;

    if (ltv_len == 0) {
        return false;
    }

    *ltv_out = ltv_start;
    *ltv_len_out = ltv_len;
    return true;
}

/**
 * @brief Extract LTV pointer and length from BIS configuration (BASE)
 *
 * BIS configuration contains AD structures:
 *   - BIG Info (AD type 0x2C) - skipped
 *   - Service Data (AD type 0x16) with UUID 0x1851 containing BASE
 *
 * BASE structure:
 *   - 3 bytes: Presentation_Delay
 *   - 1 byte:  Num_Subgroups
 *   Per subgroup:
 *     - 1 byte:  Num_BIS
 *     - 5 bytes: Codec_ID
 *     - 1 byte:  Codec_Specific_Configuration_Length
 *     - N bytes: Codec_Specific_Configuration (LTVs)
 *     - ...
 */
static bool parse_bis_container(const void* config, uint32_t config_len, const uint8_t** ltv_out,
                                size_t* ltv_len_out) {
    const uint8_t* p = (const uint8_t*)config;
    const uint8_t* end = p + config_len;

    /* Iterate through AD structures */
    while (p + 2 < end) {
        uint8_t ad_len = p[0];
        uint8_t ad_type = p[1];

        if (ad_len == 0 || p + 1 + ad_len > end) {
            break;
        }

        /* Look for Service Data with Basic Audio Announcement UUID */
        if (ad_type == AD_TYPE_SERVICE_DATA && ad_len >= 4) {
            uint16_t uuid = read_le16(p + 2);

            if (uuid == UUID_BASIC_AUDIO_ANNOUNCEMENT) {
                const uint8_t* base = p + 4; /* After: len, type, UUID[2] */
                const uint8_t* base_end = p + 1 + ad_len;

                /* Minimum BASE size: 3 (delay) + 1 (num_subgroups) + 1 (num_bis) + 5 (codec_id) + 1
                 * (cfg_len) */
                if (base + 11 > base_end) {
                    return false;
                }

                const uint8_t* ptr = base + 3; /* Skip Presentation_Delay */
                uint8_t num_subgroups = *ptr++;

                if (num_subgroups == 0) {
                    return false;
                }

                /* Parse first subgroup */
                ptr++;    /* Skip Num_BIS */
                ptr += 5; /* Skip Codec_ID */

                if (ptr >= base_end) {
                    return false;
                }

                uint8_t ltv_len = *ptr++;

                if (ptr + ltv_len > base_end) {
                    ltv_len = (uint8_t)(base_end - ptr);
                }

                if (ltv_len == 0) {
                    return false;
                }

                *ltv_out = ptr;
                *ltv_len_out = ltv_len;
                return true;
            }
        }

        p += 1 + ad_len;
    }

    return false;
}

/*------------------------------------------------------------------------------
 * Decoder Initialisation
 *----------------------------------------------------------------------------*/

/**
 * @brief Create and initialise LC3 decoders for all channels
 */
static bool init_decoders(LC3_stream* stream) {
    const LC3_config* cfg = &stream->config;

    unsigned dec_size = lc3_decoder_size(cfg->frame_duration_us, cfg->sample_rate_hz);
    if (dec_size == 0) {
        return false;
    }

    stream->samples_per_frame = lc3_frame_samples(cfg->frame_duration_us, cfg->sample_rate_hz);

    /* Allocate PCM output buffer */
    stream->pcm_buffer_bytes = stream->samples_per_frame * cfg->channels * sizeof(int16_t);
    stream->pcm_buffer = (int16_t*)malloc(stream->pcm_buffer_bytes);
    if (!stream->pcm_buffer) {
        return false;
    }

    /* Create per-channel decoders */
    for (uint8_t ch = 0; ch < cfg->channels; ++ch) {
        stream->decoder_mem[ch] = malloc(dec_size);
        if (!stream->decoder_mem[ch]) {
            return false;
        }

        stream->decoder[ch] =
            lc3_setup_decoder(cfg->frame_duration_us, cfg->sample_rate_hz, 0, /* No resampling */
                              stream->decoder_mem[ch]);
        if (!stream->decoder[ch]) {
            return false;
        }
    }

    return true;
}

/*------------------------------------------------------------------------------
 * API Implementation
 *----------------------------------------------------------------------------*/

extern "C" {

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void) {
    return (bluespy_audio_codec_lib_info){.api_version = BLUESPY_AUDIO_API_VERSION,
                                          .codec_name = "LC3"};
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret
new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info) {
    bluespy_audio_codec_init_ret ret = {
        .error = -1, .format = {0}, .fns = {0}, .context_handle = 0};

    /* Validate configuration */
    if (!info || !info->config || info->config_len == 0) {
        return ret;
    }
    if (info->container != BLUESPY_CODEC_CIS && info->container != BLUESPY_CODEC_BIS) {
        return ret;
    }

    /* Dry run to allow the host to check if this codec format is supported */
    if (stream_id == BLUESPY_ID_INVALID) {
        ret.error = 0;
        return ret;
    }

    /* Allocate stream handle */
    LC3_stream* stream = (LC3_stream*)calloc(1, sizeof(LC3_stream));
    if (!stream) {
        ret.error = -2;
        return ret;
    }

    stream->parent_stream_id = stream_id;

    /* Extract LTV configuration from container */
    const uint8_t* ltv = NULL;
    size_t ltv_len = 0;
    bool parsed;

    if (info->container == BLUESPY_CODEC_CIS) {
        parsed = parse_cis_container(info->config, info->config_len, &ltv, &ltv_len);
    } else {
        parsed = parse_bis_container(info->config, info->config_len, &ltv, &ltv_len);
    }

    if (!parsed) {
        stream_free_resources(stream);
        free(stream);
        ret.error = -3;
        return ret;
    }

    /* Parse codec configuration */
    parse_ltv_config(&stream->config, ltv, ltv_len);

    /* Enforce channel limit */
    if (stream->config.channels > MAX_CHANNELS) {
        stream->config.channels = MAX_CHANNELS;
    }

    /* Initialise decoders */
    if (!init_decoders(stream)) {
        stream_free_resources(stream);
        free(stream);
        ret.error = -4;
        return ret;
    }

    /* Success */
    ret.error = 0;
    ret.context_handle = (uintptr_t)stream;

    ret.format.sample_rate = stream->config.sample_rate_hz;
    ret.format.n_channels = stream->config.channels;
    ret.format.sample_format = BLUESPY_AUDIO_FORMAT_S16_LE;
    ret.fns.decode = codec_decode;
    ret.fns.deinit = codec_deinit;

    return ret;
}

BLUESPY_CODEC_API void codec_decode(uintptr_t context, const uint8_t* payload, uint32_t payload_len,
                                    bluespy_event_id event_id, uint64_t sequence_number) {
    LC3_stream* stream = (LC3_stream*)context;
    if (!stream || !payload || payload_len == 0) {
        return;
    }

    const LC3_config* cfg = &stream->config;
    const uint8_t channels = cfg->channels;
    const uint16_t octets_per_frame = cfg->octets_per_frame;
    const size_t samples = stream->samples_per_frame;
    int16_t* pcm = stream->pcm_buffer;

    /* Clear output buffer */
    memset(pcm, 0, stream->pcm_buffer_bytes);

    /* Decode each channel - LC3 frames are concatenated in channel order */
    for (uint8_t ch = 0; ch < channels; ++ch) {
        size_t offset = (size_t)ch * octets_per_frame;

        if (offset >= payload_len) {
            /* No data for this channel - generate PLC (Packet Loss Concealment) */
            lc3_decode(stream->decoder[ch], NULL, octets_per_frame, LC3_PCM_FORMAT_S16, pcm + ch,
                       channels);
            continue;
        }

        /* Calculate available bytes for this frame */
        size_t available = payload_len - offset;
        size_t frame_bytes = (available < octets_per_frame) ? available : octets_per_frame;

        /*
         * Decode frame into interleaved buffer.
         * Output pointer is offset by channel index, stride equals total channels.
         */
        int result = lc3_decode(stream->decoder[ch], payload + offset, (uint16_t)frame_bytes,
                                LC3_PCM_FORMAT_S16, pcm + ch, channels);

        /* If decode failed, the decoder automatically produces PLC output */
        (void)result;
    }

    /* Deliver decoded audio to host */
    bluespy_add_audio((const uint8_t*)pcm, (uint32_t)stream->pcm_buffer_bytes, event_id, 0);

    /* Update sequence tracking */
    stream->last_seq = sequence_number;
    stream->have_seq = true;
}

BLUESPY_CODEC_API void codec_deinit(uintptr_t context) {
    LC3_stream* stream = (LC3_stream*)context;
    if (stream) {
        stream_free_resources(stream);
        free(stream);
    }
}

} // end extern "C"