// Copyright RF Creations Ltd 2026
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

/**
 * @file bluespy_codec_utils.h
 * @brief Shared utilities for blueSPY codec plugins
 */

#ifndef BLUESPY_CODEC_UTILS_H
#define BLUESPY_CODEC_UTILS_H

#include <stdint.h>

#define RTP_HEADER_MIN_SIZE 12
#define RTP_SEQ_OFFSET 2

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a little-endian uint16 from a buffer
 */
static inline uint16_t read_le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

/**
 * @brief Read a little-endian uint32 from a buffer
 */
static inline uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/**
 * @brief Read a big-endian uint16 from a buffer
 */
static inline uint16_t read_be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

/**
 * @brief Calculate the difference between two 16-bit sequence numbers,
 * handling standard 16-bit integer rollover.
 * * @return Positive if current > last, negative if current < last.
 */
static inline int32_t calculate_rtp_seq_diff(uint16_t current_seq, uint16_t last_seq) {
    int32_t diff = (int32_t)current_seq - (int32_t)last_seq;

    if (diff < -32768) {
        diff += 65536;
    } else if (diff > 32768) {
        diff -= 65536;
    }

    return diff;
}

/**
 * @brief Calculate RTP header length including CSRC fields
 *
 * @param payload     Pointer to RTP packet
 * @param payload_len Total payload length
 * @return Header length in bytes, or 0 if invalid
 */
static inline uint32_t get_rtp_header_length(const uint8_t* payload, uint32_t payload_len) {
    if (payload_len < RTP_HEADER_MIN_SIZE) {
        return 0;
    }

    uint32_t csrc_count = payload[0] & 0x0F;
    uint32_t header_len = RTP_HEADER_MIN_SIZE + (4 * csrc_count);

    return (header_len >= payload_len) ? 0 : header_len;
}

#ifdef __cplusplus
}
#endif

#endif // BLUESPY_CODEC_UTILS_H