cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
project(bluespy_codecs)

# set(CMAKE_CXX_STANDARD 11)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)

add_library(bluespy_codecs INTERFACE)
target_include_directories(bluespy_codecs INTERFACE include)

add_library(bluespy_codec_build INTERFACE)
target_link_libraries(bluespy_codec_build INTERFACE bluespy_codecs)
target_compile_definitions(bluespy_codec_build INTERFACE BLUESPY_CODEC_BUILD)

# Build AAC
add_library(aac SHARED
    AAC.c
)
add_subdirectory(fdk-aac-stripped EXCLUDE_FROM_ALL)
target_link_libraries(aac PRIVATE fdk-aac bluespy_codec_build)

# #Build aptX
# add_library(aptx SHARED
#     aptx.cpp
#     libfreeaptx/freeaptx.c
# )
# target_link_libraries(aptx PRIVATE bluespy_codec_build)
# target_include_directories(aptx PRIVATE libfreeaptx)