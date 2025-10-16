#include "bluespy_codec_interface.h"
#include "ldacdec.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

BLUESPY_CODEC_API bluespy_audio_codec_lib_info init() {
    return (bluespy_audio_codec_lib_info) {.api_version = BLUESPY_AUDIO_API_VERSION, .codec_name = "LDAC"};
}

#define MAX_STREAMS 16

static struct LDAC_handle {
    ldacdec_t dec;
    bool initialized;
    uint32_t sample_rate;
    uint8_t n_channels;
    uint32_t sequence_number;
    bluespy_audiostream_id stream_id;
    int in_use;
    int16_t pcm_buf[32768];
} handles[MAX_STREAMS] = {0};

static struct LDAC_handle* get_handle(bluespy_audiostream_id id) {
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
            handles[i].initialized = false;
            handles[i].sequence_number = (uint32_t)-1;
            memset(&handles[i].dec, 0, sizeof(handles[i].dec));
            return &handles[i];
        }
    }
    
    return NULL;
}

BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id,
                                                          const bluespy_audio_codec_info* info) {
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

    if (vendor_id == 0x0000012D && vendor_codec_id == 0xAA) {
        // LDAC (Sony)
    } else {
        return r;
    }
    
    if (info->data.AVDTP.len < 2) {
        r.error = -2;
        return r;
    }

    struct LDAC_handle* handle = get_handle(id);
    if (!handle) {
        r.error = -10; // Too many concurrent streams
        return r;
    }

    const uint8_t *config = cap->Media_Codec_Specific_Information;
    uint8_t freq_bits = config[0] & 0x3F;
    uint8_t ch_mode_bits = (config[0] >> 6) & 0x03;

    if (freq_bits & 0x20)
        r.format.sample_rate = 96000;
    else if (freq_bits & 0x10)
        r.format.sample_rate = 88200;
    else if (freq_bits & 0x08)
        r.format.sample_rate = 48000;
    else if (freq_bits & 0x04)
        r.format.sample_rate = 44100;
    else
        r.format.sample_rate = 48000;
    
    switch (ch_mode_bits) {
        case 0:  r.format.n_channels = 2; break;
        case 1:  r.format.n_channels = 2; break;
        case 2:  r.format.n_channels = 1; break;
        default: r.format.n_channels = 2; break;
    }

    r.format.bits_per_sample = 16;

    memset(&handle->dec, 0, sizeof(handle->dec));
    if (ldacdecInit(&handle->dec) < 0) {
        handle->in_use = 0;
        return r;
    }
    
    handle->sample_rate = r.format.sample_rate;
    handle->n_channels = r.format.n_channels;
    handle->sequence_number = (uint32_t)-1;
    handle->initialized = true;
    
    r.error = 0;
    r.fns.decode = codec_decode;
    r.fns.deinit = codec_deinit;

    return r;
}

BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id) {
    struct LDAC_handle* handle = get_handle(id);
    if (handle) {
        memset(&handle->dec, 0, sizeof(handle->dec));
        handle->initialized = false;
        handle->in_use = 0;
    }
}

BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(
    bluespy_audiostream_id id,
    const uint8_t* payload,
    const uint32_t payload_len,
    bluespy_event_id event_id)
{
    bluespy_audio_codec_decoded_audio out = { .data = NULL, .len = 0 };

    struct LDAC_handle* handle = get_handle(id);
    if (!handle || !handle->initialized) {
        return out;
    }

    if (payload_len < 20) {
        return out;
    }

    const uint8_t* frame = payload;
    uint32_t len_left = payload_len;

    // Strip RTP header
    uint32_t csrc_count = payload[0] & 0x0F;
    uint32_t rtp_hdr = 12 + 4 * csrc_count;
    if (len_left <= rtp_hdr) {
        return out;
    }

    frame += rtp_hdr;
    len_left -= rtp_hdr;

    // Find first LDAC sync (0xAA)
    uint32_t sync_offset = 0;
    while (sync_offset < len_left && frame[sync_offset] != 0xAA)
        sync_offset++;

    if (sync_offset >= len_left) {
        return out;
    }

    frame += sync_offset;
    len_left -= sync_offset;

    // Use per-stream buffer
    int16_t* pcm_buf = handle->pcm_buf;
    int16_t* pcm_write = pcm_buf;
    size_t total_bytes_written = 0;

    while (len_left > 0) {
        int bytes_used = 0;
        int ret = ldacDecode(&handle->dec, (uint8_t*)frame, pcm_write, &bytes_used);

        if (ret < 0) {
            // Attempt soft resync
            uint32_t resync_off = 1;
            while (resync_off < len_left && frame[resync_off] != 0xAA)
                resync_off++;

            if (resync_off >= len_left) {
                break;
            }

            frame += resync_off;
            len_left -= resync_off;
            continue;
        }

        if (bytes_used <= 0) {
            break;
        }

        frame += bytes_used;
        if (len_left < (uint32_t)bytes_used)
            break;
        len_left -= bytes_used;

        int frame_samples = handle->dec.frame.frameSamples;
        int channels = handle->dec.frame.channelCount;
        int bytes_out = frame_samples * channels * sizeof(int16_t);

        pcm_write += frame_samples * channels;
        total_bytes_written += bytes_out;

        if ((size_t)(pcm_write - pcm_buf) >= sizeof(handle->pcm_buf)/sizeof(handle->pcm_buf[0])) {
            break;
        }
    }

    if (total_bytes_written == 0) {
        return out;
    }

    handle->sample_rate = ldacdecGetSampleRate(&handle->dec);
    handle->n_channels  = (uint8_t)ldacdecGetChannelCount(&handle->dec);

    out.data = (uint8_t*)pcm_buf;
    out.len  = (uint32_t)total_bytes_written;
    out.source_id = event_id;
    return out;
}

#ifdef __cplusplus
}
#endif