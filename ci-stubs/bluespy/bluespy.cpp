/**
 * @file bluespy.cpp
 * @brief Stub version of bluespy.cpp for GitHub Actions CI
 */

#include "bluespy.h"

#ifdef _WIN32
#ifdef BLUESPY_BUILD_DLL
#define BLUESPY_API __declspec(dllexport)
#else
#define BLUESPY_API __declspec(dllimport)
#endif
#else
#define BLUESPY_API __attribute__((visibility("default")))
#endif

extern "C" {
BLUESPY_API void bluespy_add_audio(const uint8_t*, uint32_t, bluespy_event_id, uint32_t) {}
}