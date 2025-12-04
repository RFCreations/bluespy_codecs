/**
 * @file bluespy.cpp
 * @brief Stub version of bluespy.cpp for GitHub Actions CI
 */

#include "bluespy.h"

extern "C" {

BLUESPY_API void bluespy_add_decoded_audio(const uint8_t*, uint32_t, bluespy_event_id) {}
BLUESPY_API void bluespy_add_continuous_audio(const uint8_t*, uint32_t, bluespy_event_id) {}

}