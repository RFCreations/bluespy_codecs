// Copyright RF Creations Ltd 2023
// Distributed under the Boost Software License, Version 1.0. (See accompanying file LICENSE)

#ifndef BLUESPY_CODEC_INTERFACE_H
#define BLUESPY_CODEC_INTERFACE_H

#include "stdint.h"
#include "bluespy.h"

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

/**
 * @brief bluespy_codec_info
 * @return bluespy_codec_info_return
 *
 * Return generic information about the codec
 */
BLUESPY_CODEC_API bluespy_audio_codec_lib_info init();

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
BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id id, const bluespy_audio_codec_info* info);

/**
 * @brief bluespy_codec_deinit
 * @param[in] transport Type of bluetooth audio
 * @param[in] media_codec_type Codec identifier, e.g. Audio Codec ID (BLUESPY_CODEC_A2DP_TYPES)
 * @param[in] codec_specific_data Opaque block of data from setup
 * @param[in] codec_specific_data_len
 * @return bluespy_audio_codec_deinit_t object.
 *
 * This function deinitialises a codec.
 */
BLUESPY_CODEC_API void codec_deinit(bluespy_audiostream_id id);

/**
 * @brief bluespy_codec_decode
 * @param[in] handle
 * @param[in] coded_data
 * @param[in] coded_len
 * @param[out] uncoded_data The output should be 16 bit audio data, channels interleaved
 * @param[in] uncoded_len The total space in the output buffer, not per channel.
 * @param[in] event_id Unique identifier for this event from the capture system.
 * @return Total number of returned samples, or BLUESPY_CODEC_ERRORS if negative.
 *
 * This function decodes an on air frame of codec data and produces a frame of audio into an
 * external buffer. For A2DP, the coded_data will point to the start of the RTP header.
 */
BLUESPY_CODEC_API bluespy_audio_codec_decoded_audio codec_decode(bluespy_audiostream_id id, 
                                                                 const uint8_t* payload,
                                                                 const uint32_t payload_len,
                                                                 bluespy_event_id event_id);

#ifdef __cplusplus
}
#endif

#endif