// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#ifndef BLUESPY_CODEC_INTERFACE_H
#define BLUESPY_CODEC_INTERFACE_H

#include "stdint.h"

#ifndef BLUESPY_CODEC_DLL_IMPORT
#if defined _WIN32 || defined __CYGWIN__
#define BLUESPY_CODEC_DLL_IMPORT __declspec(dllimport)
#define BLUESPY_CODEC_DLL_EXPORT __declspec(dllexport)
#else
#if __GNUC__ >= 4
#define BLUESPY_CODEC_DLL_IMPORT __attribute__((visibility("default")))
#define BLUESPY_CODEC_DLL_EXPORT __attribute__((visibility("default")))
#else
#define BLUESPY_CODEC_DLL_IMPORT
#define BLUESPY_CODEC_DLL_EXPORT
#endif
#endif
#endif

#ifdef BLUESPY_CODEC_BUILD // defined if we are building the DLL (instead of using it)
#define BLUESPY_CODEC_API BLUESPY_CODEC_DLL_EXPORT
#else
#define BLUESPY_CODEC_API BLUESPY_CODEC_DLL_IMPORT
#endif // BLUESPY_CODEC_BUILD

#ifdef __cplusplus
extern "C" {
#endif

struct bluespy_codec_handle;

enum BLUESPY_CODEC_TRANSPORT { BLUESPY_CODEC_A2DP = 1 };

// From Bluetooth Assigned Numbers
enum BLUESPY_CODEC_A2DP_TYPES {
    BLUESPY_CODEC_A2DP_SBC = 0,
    BLUESPY_CODEC_A2DP_MPEG_12_Audio = 1,
    BLUESPY_CODEC_A2DP_MPEG_24_AAC = 2,
    BLUESPY_CODEC_A2DP_MPEG_D_USAC = 3,
    BLUESPY_CODEC_A2DP_ATRAC_Family = 4,
    BLUESPY_CODEC_A2DP_LC3 = 6,
    BLUESPY_CODEC_A2DP_Non_A2DP = 0xFF,
};

enum BLUESPY_CODEC_ERRORS {
    BLUESPY_CODEC_SUCCESS = 0,

    // The output buffer needs more space, retry
    BLUESPY_CODEC_BUFFER_TOO_SMALL = -1,

    // This frame is undecodable, but we should try the next one
    BLUESPY_CODEC_RECOVERABLE_ERROR = -2,

    // The stream is broken, stop trying
    BLUESPY_CODEC_UNRECOVERABLE_ERROR = -3,

    // The stream has ended
    BLUESPY_CODEC_END_OF_STREAM = -4,

    // Unsupported codec
    BLUESPY_CODEC_UNSUPPORTED_CODEC = -5,
};

struct bluespy_codec_info_return {
    int api_version; // Set to 1
    const char* codec_name;
};

/**
 * @brief bluespy_codec_info
 * @return bluespy_codec_info_return
 *
 * Return generic information about the codec
 */
BLUESPY_CODEC_API bluespy_codec_info_return bluespy_codec_info();

struct bluespy_codec_init_return {
    BLUESPY_CODEC_ERRORS result;
    bluespy_codec_handle* handle;
    const char* codec_name;

    /* 'seek_pre_frames' is how many previous frames affect the
     * current frame output for this codec. On seeking this many frames will be run through the
     * decoder before presenting to the user. It is usually 0 or 1. */
    unsigned seek_pre_frames;
    unsigned sample_rate; // In Hz
    unsigned channels;
    // Output buffer uncoded_len is:
    // max(min_output_size, 8 * coded_len * channels * sample_rate / min_bitrate)
    unsigned min_output_size;
    unsigned min_bitrate; // Set to 0xFFFFFFFF if unknown
};

/**
 * @brief bluespy_codec_init
 * @param[in] transport Type of bluetooth audio
 * @param[in] media_codec_type Codec identifier, e.g. Audio Codec ID (BLUESPY_CODEC_A2DP_TYPES)
 * @param[in] codec_specific_data Opaque block of data from setup
 * @param[in] codec_specific_data_len
 * @return bluespy_codec_init_return object containing a handle or error.
 *
 * This function initialises a codec from the bluetooth configuration, and returns a handle to the
 * decoder or an error.
 */
BLUESPY_CODEC_API bluespy_codec_init_return bluespy_codec_init(BLUESPY_CODEC_TRANSPORT transport,
                                                               int media_codec_type,
                                                               const void* codec_specific_data,
                                                               int codec_specific_data_len);

/**
 * @brief bluespy_codec_deinit
 * @param[in] handle
 *
 * This is called once on each handle after it is no longer needed so any resources can be freed
 */
BLUESPY_CODEC_API void bluespy_codec_deinit(bluespy_codec_handle* handle);

/**
 * @brief bluespy_codec_decode
 * @param[in] handle
 * @param[in] coded_data
 * @param[in] coded_len
 * @param[out] uncoded_data The output should be 16 bit audio data, channels interleaved
 * @param[in] uncoded_len The total space in the output buffer, not per channel.
 * @return Total number of returned samples, or BLUESPY_CODEC_ERRORS if negative.
 *
 * This function decodes an on air frame of codec data and produces a frame of audio into an
 * external buffer. For A2DP, the coded_data will point to the start of the RTP header.
 */
BLUESPY_CODEC_API int bluespy_codec_decode(bluespy_codec_handle* handle, const uint8_t* coded_data,
                                           int coded_len, int16_t* uncoded_data, int uncoded_len);

#ifdef __cplusplus
}
#endif

#endif