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
 * @brief Initialise a codec plug-in library.
 *
 * Called once immediately after the shared library is loaded.
 * The implementation must populate the returned structure with its
 * API version (must equal BLUESPY_AUDIO_API_VERSION) and a 
 * human-readable codec name.
 * 
 * Example:
 * @code
 * BLUESPY_CODEC_API bluespy_audio_codec_lib_info init() {
 *     return (bluespy_audio_codec_lib_info){
 *         .api_version = BLUESPY_AUDIO_API_VERSION,
 *         .codec_name = "AAC"
 *     };
 * }
 * @endcode
 * 
 * @return codec library information.
 */
BLUESPY_CODEC_API bluespy_audio_codec_lib_info init(void);

/**
 * @brief Create and configure a new codec instance for a detected stream.
 * 
 * The host calls this function when an audio stream has been discovered and requires decoding.
 * 
 * @param stream_id Unique identifier for reference/logging.
 * @param info Pointer to codec configuration.
 * 
 * @return 
 *  - On success: structure with error == 0 and valid function pointers. 
 *  - On failure: structure with error < 0; no resources must remain allocated.
 */
BLUESPY_CODEC_API bluespy_audio_codec_init_ret new_codec_stream(bluespy_audiostream_id stream_id, const bluespy_audio_codec_info* info);


/**
 * @brief Decode on codec frame or sequence from the capture. 
 * 
 * @param[in] context Opaque handle to the codec instance state.
 * @param[in] payload Pointer to encoded bytes.
 * @param[in] payload_len Length in bytes of @p payload.
 * @param[out] event_id Capture event identifier corresponding to this SDU.
 * @param sequence_number 64-bit monotonically increasing sequence counter for this SDU, assigned by the host.
 * 
 * @note
 *   - **Classic (AVDTP/A2DP):**  
 *     Each call represents one L2CAP SDU = one AVDTP Media Packet, usually
 *     containing an RTP header (12 + 4xCSRC bytes) followed by one or more
 *     codec frames.
 *   - **LE Audio (CIS/BIS):**
 *     Each call provides a reconstructed ISOAL SDU as seen at the host layer.
 *     Depending on ISOAL segmentation rules, one SDU can hold multiple codec
 *     frames or a partial frame. The decoder must handle reconstruction.
 */

BLUESPY_CODEC_API void codec_decode(uintptr_t context, 
                                    const uint8_t* payload,
                                    const uint32_t payload_len,
                                    bluespy_event_id event_id,
                                    uint64_t sequence_number);
    
/**
 * @brief De-initialise and release all state for a codec stream.
 * 
 * Implementations must cast @p context back to their internal state pointer,
 * free any dynamic allocations, and close decoder handles.
 * 
 * @param[in] context Opaque handle to the codec instance state.
 */
BLUESPY_CODEC_API void codec_deinit(uintptr_t context);

#ifdef __cplusplus
}
#endif

#endif