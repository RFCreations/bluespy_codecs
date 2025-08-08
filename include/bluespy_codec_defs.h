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

#define BLUESPY_CODEC_API BLUESPY_CODEC_DLL_EXPORT

#endif