cmake_minimum_required(VERSION 3.20)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
project(bluespy_codecs)

option(BUILD_AAC "Build AAC codec DLL" ON)
option(BUILD_APTX "Build aptX codec DLL" ON)
option(BUILD_LDAC "Build LDAC codec DLL" ON)
option(BUILD_LC3 "Build LC3 codec DLL" ON)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(BUILD_SHARED_LIBS OFF)

option(USE_STUB_BLUESPY_CPP "Use stub library for blueSPY (for CI builds)" OFF)

# --------- Set path to libblueSPY.lib here ---------
set(LIB_BLUESPY_PATH "C:/Path/to/Repos/libblueSPY/libblueSPY.lib")

# --------- Find bluespy.h in blueSPY install directory or use ci-stubs version for CI ---------
find_path(BLUESPY_INCLUDE_DIR 
          NAMES bluespy.h
          PATHS "C:/Program Files/RFcreations/blueSPY")

if (BLUESPY_INCLUDE_DIR)
    message(STATUS "Found blueSPY include dir: ${BLUESPY_INCLUDE_DIR}")
else()
    message(WARNING "bluespy.h not found in system. Falling back to ci-stubs copy.")
    set(BLUESPY_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/ci-stubs/bluespy")
endif()

include_directories(${BLUESPY_INCLUDE_DIR})

if (USE_STUB_BLUESPY_CPP OR NOT EXISTS "${LIB_BLUESPY_PATH}")
    add_library(libblueSPY_stub STATIC ${CMAKE_SOURCE_DIR}/ci-stubs/bluespy/bluespy.cpp)
    target_compile_definitions(libblueSPY_stub PUBLIC BLUESPY_API=)
    set(LIB_BLUESPY_PATH libblueSPY_stub)
else()
    message(STATUS "Using real libblueSPY at: ${LIB_BLUESPY_PATH}")
endif()

# ---- Interface libraries ----
add_library(bluespy_codecs INTERFACE)
target_include_directories(bluespy_codecs INTERFACE include)

add_library(bluespy_codec_build INTERFACE)
target_link_libraries(bluespy_codec_build INTERFACE bluespy_codecs)
target_compile_definitions(bluespy_codec_build INTERFACE BLUESPY_CODEC_BUILD)

# ---- Codec builds ----
# Build AAC if enabled
if(BUILD_AAC)
    # Force fdk-aac to build statically, no DLL
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    add_library(aac SHARED
        AAC.c
    )
    add_subdirectory(fdk-aac-stripped EXCLUDE_FROM_ALL)
    target_link_libraries(aac PRIVATE fdk-aac bluespy_codec_build)
    target_link_libraries(aac PRIVATE ${LIB_BLUESPY_PATH}) 
endif()

# Build aptX if enabled
if(BUILD_APTX)
    add_library(aptx SHARED
        APTX.c
        libfreeaptx/freeaptx.c
    )
    target_link_libraries(aptx PRIVATE bluespy_codec_build)
    target_link_libraries(aptx PRIVATE ${LIB_BLUESPY_PATH})
    target_include_directories(aptx PRIVATE libfreeaptx)
endif()

# Build LDAC if enabled
if(BUILD_LDAC)
    add_library(ldac SHARED
        LDAC.c
        libldacdec/libldacdec.c
        libldacdec/bit_allocation.c
        libldacdec/huffCodes.c
        libldacdec/bit_reader.c
        libldacdec/utility.c
        libldacdec/imdct.c
        libldacdec/spectrum.c
    )

    target_include_directories(ldac PRIVATE
        libldacdec
    )

    if (UNIX)
        target_link_libraries(ldac PRIVATE m)
    endif()

    target_link_libraries(ldac PRIVATE bluespy_codec_build)
    target_link_libraries(ldac PRIVATE ${LIB_BLUESPY_PATH})
endif()

# Build LC3 if enabled
if(BUILD_LC3)
    file(GLOB LIBLC3_SRC
        liblc3/src/*.c
        # Optional: include subdirectories if needed
        liblc3/src/*.s
    )

    add_library(liblc3 STATIC ${LIBLC3_SRC})
    target_include_directories(liblc3 PUBLIC liblc3/include)

    add_library(lc3 SHARED
        LC3.c
    )
    target_include_directories(lc3 PRIVATE
        liblc3/include
    )

    if (UNIX)
        target_link_libraries(liblc3 PRIVATE m)
        target_link_libraries(lc3 PRIVATE m)
    endif()

    target_link_libraries(lc3 PRIVATE
        bluespy_codec_build
        liblc3
    )
    target_link_libraries(lc3 PRIVATE ${LIB_BLUESPY_PATH})
endif()