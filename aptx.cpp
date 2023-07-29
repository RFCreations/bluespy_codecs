// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#include "bluespy_codec_interface.h"

extern "C" {
#include "freeaptx.h"
}
#include <cstring>
#include <vector>

bluespy_codec_info_return bluespy_codec_info() { return {1, "aptX"}; }

struct bluespy_codec_handle {
    struct aptx_context* aptx = nullptr;
    bool hd = false;
    std::vector<uint8_t> output;

    bluespy_codec_handle(bool hd) : aptx(aptx_init(hd)), hd(hd) {}
    bluespy_codec_handle(const bluespy_codec_handle&) = delete;
    bluespy_codec_handle& operator=(const bluespy_codec_handle&) = delete;
    ~bluespy_codec_handle() { aptx_finish(aptx); }
};

bluespy_codec_init_return bluespy_codec_init(BLUESPY_CODEC_TRANSPORT transport,
                                             int media_codec_type, const void* codec_specific_data,
                                             int codec_specific_data_len) {
    bluespy_codec_init_return r{BLUESPY_CODEC_UNSUPPORTED_CODEC};
    bool hd = false;

    if (transport != BLUESPY_CODEC_A2DP || media_codec_type != BLUESPY_CODEC_A2DP_Non_A2DP ||
        codec_specific_data_len < 7)
        return r;

    uint32_t vendor;
    uint16_t codec_id;
    memcpy(&vendor, codec_specific_data, 4);
    memcpy(&codec_id, (const char*)codec_specific_data + 4, 2);

    if (vendor == 0x4F && codec_id == 0x1) {
        r.codec_name = "aptX";
    } else if (vendor == 0xD7 && codec_id == 0x24) {
        r.codec_name = "aptX HD";
        hd = true;
    } else if ((vendor == 0xD7 || vendor == 0xA) && codec_id == 0x2) {
        r.codec_name = "aptX LL";
    } else {
        return r;
    }

    uint8_t codec_info = *((const uint8_t*)codec_specific_data + 6);

    switch (codec_info & 0xF) {
    case 1:
        // r.channels = 1;
        return r;
    case 2:
        r.channels = 2;
        break;
    default:
        return r;
    }

    switch (codec_info >> 4) {
    case 1:
        r.sample_rate = 48000;
        break;
    case 2:
        r.sample_rate = 44100;
        break;
    case 4:
        r.sample_rate = 32000;
        break;
    case 8:
        r.sample_rate = 16000;
        break;
    default:
        return r;
    }

    r.handle = new bluespy_codec_handle{hd};
    r.result = BLUESPY_CODEC_SUCCESS;
    r.min_output_size = 4;
    r.min_bitrate = hd ? 12 * r.sample_rate : 8 * r.sample_rate;

    return r;
}

void bluespy_codec_deinit(bluespy_codec_handle* handle) { delete handle; }

int bluespy_codec_decode(bluespy_codec_handle* handle, const uint8_t* coded_data, int coded_len,
                         int16_t* uncoded_data, int uncoded_len) {
    if (coded_len > 0 && handle->hd) { // Remove RTP header - only present on aptX HD
        uint32_t rtp_header_len = 12 + 4 * (*coded_data & 0xF);

        if (coded_len < rtp_header_len)
            return BLUESPY_CODEC_RECOVERABLE_ERROR;

        coded_data += rtp_header_len;
        coded_len -= rtp_header_len;
    }

    size_t out_total_samples = 8 * (handle->hd ? coded_len / 6 : coded_len / 4);

    if (uncoded_len < out_total_samples) {
        return BLUESPY_CODEC_BUFFER_TOO_SMALL;
    }

    handle->output.resize(3 * out_total_samples);

    size_t written = 0;
    aptx_decode(handle->aptx, coded_data, coded_len, handle->output.data(), handle->output.size(),
                &written);

    for (int i = 0; i < written; i += 3) {
        *uncoded_data++ = (int16_t)handle->output[i + 1] | ((int16_t)handle->output[i + 2] << 8);
    }

    return written / 3;
}
