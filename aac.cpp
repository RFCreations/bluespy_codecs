// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"

extern "C" {
#include "aacdecoder_lib.h"
}
#include <cstring>
#include <memory>
#include <vector>

bluespy_codec_info_return bluespy_codec_info() { return {1, "AAC"}; }

struct bluespy_codec_handle {
    HANDLE_AACDECODER aac = nullptr;
    uint32_t sequence_number = -1;

    bluespy_codec_handle() : aac(aacDecoder_Open(TT_MP4_LATM_MCP1, 1)) {}
    bluespy_codec_handle(const bluespy_codec_handle&) = delete;
    bluespy_codec_handle& operator=(const bluespy_codec_handle&) = delete;
    ~bluespy_codec_handle() { aacDecoder_Close(aac); }
};

bluespy_codec_init_return bluespy_codec_init(BLUESPY_CODEC_TRANSPORT transport,
                                             int media_codec_type, const void* codec_specific_data,
                                             int codec_specific_data_len) {
    bluespy_codec_init_return r{BLUESPY_CODEC_UNSUPPORTED_CODEC};
    r.seek_pre_frames = 1;

    if (transport != BLUESPY_CODEC_A2DP || media_codec_type != BLUESPY_CODEC_A2DP_MPEG_24_AAC ||
        codec_specific_data_len < 6)
        return r;

    struct {
        uint8_t object_type;
        uint8_t sample_rate_ls;
        uint8_t chan_sample_rate;
        uint8_t bit_rate_ls_vbr;
        uint8_t bit_rate_mid;
        uint8_t bit_rate_ms;
    } codec_data;

    memcpy(&codec_data, codec_specific_data, 6);

    if (codec_data.sample_rate_ls >> 7 & 1) {
        r.sample_rate = 8000;
    } else if (codec_data.sample_rate_ls >> 6 & 1) {
        r.sample_rate = 11025;
    } else if (codec_data.sample_rate_ls >> 5 & 1) {
        r.sample_rate = 12000;
    } else if (codec_data.sample_rate_ls >> 4 & 1) {
        r.sample_rate = 16000;
    } else if (codec_data.sample_rate_ls >> 3 & 1) {
        r.sample_rate = 22050;
    } else if (codec_data.sample_rate_ls >> 2 & 1) {
        r.sample_rate = 24000;
    } else if (codec_data.sample_rate_ls >> 1 & 1) {
        r.sample_rate = 32000;
    } else if (codec_data.sample_rate_ls & 1) {
        r.sample_rate = 44100;
    } else if (codec_data.chan_sample_rate >> 7 & 1) {
        r.sample_rate = 48000;
    } else if (codec_data.chan_sample_rate >> 6 & 1) {
        r.sample_rate = 64000;
    } else if (codec_data.chan_sample_rate >> 5 & 1) {
        r.sample_rate = 88200;
    } else if (codec_data.chan_sample_rate >> 4 & 1) {
        r.sample_rate = 96000;
    } else
        return r;

    if (codec_data.chan_sample_rate >> 2 & 1) {
        r.channels = 2;
    } else if (codec_data.chan_sample_rate >> 3 & 1) {
        r.channels = 1;
    } else {
        return r;
    }

    auto handle = std::make_unique<bluespy_codec_handle>();

    if (aacDecoder_SetParam(handle->aac, AAC_PCM_MIN_OUTPUT_CHANNELS, r.channels) != AAC_DEC_OK)
        return r;

    if (aacDecoder_SetParam(handle->aac, AAC_PCM_MAX_OUTPUT_CHANNELS, r.channels) != AAC_DEC_OK)
        return r;

    r.handle = handle.release();
    r.result = BLUESPY_CODEC_SUCCESS;
    r.min_output_size = 1024 * r.channels;
    r.min_bitrate = -1;

    return r;
}

void bluespy_codec_deinit(bluespy_codec_handle* handle) {
    std::unique_ptr<bluespy_codec_handle> p{handle};
}

int bluespy_codec_decode(bluespy_codec_handle* handle, const uint8_t* coded_data, int coded_len,
                         int16_t* uncoded_data, int uncoded_len) {
    uint16_t seq = (uint16_t)coded_data[2] << 8 | coded_data[3];

    if (coded_len > 0) { // Remove RTP header
        uint32_t rtp_header_len = 12 + 4 * (*coded_data & 0xF);

        if (coded_len < rtp_header_len)
            return BLUESPY_CODEC_RECOVERABLE_ERROR;

        coded_data += rtp_header_len;
        coded_len -= rtp_header_len;
    }

    UINT flags = 0;

    if (((seq - handle->sequence_number) & 0xFFFF) != 1 || handle->sequence_number >> 16)
        flags |= AACDEC_CLRHIST | AACDEC_INTR;

    handle->sequence_number = seq;

    uint32_t valid = coded_len, out_len = uncoded_len;

    while (valid) {
        uint32_t size = valid;
        if (aacDecoder_Fill(handle->aac, const_cast<uint8_t**>(&coded_data), &size, &valid) !=
            AAC_DEC_OK)
            return BLUESPY_CODEC_RECOVERABLE_ERROR;

        coded_data += size - valid;

        auto info = aacDecoder_GetStreamInfo(handle->aac);
        if (!info)
            return BLUESPY_CODEC_RECOVERABLE_ERROR;

        uint32_t block_size = info->frameSize * info->numChannels;

        if (out_len < block_size)
            return BLUESPY_CODEC_BUFFER_TOO_SMALL;

        if (aacDecoder_DecodeFrame(handle->aac, uncoded_data, out_len, flags) != AAC_DEC_OK)
            return BLUESPY_CODEC_RECOVERABLE_ERROR;

        uncoded_data += block_size;
        out_len -= block_size;
    }

    return uncoded_len - out_len;
}
