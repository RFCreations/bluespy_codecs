cmake_minimum_required(VERSION 3.20)

project(bluespy_codecs)

# --------- Options ---------
option(BUILD_AAC "Build AAC codec DLL" ON)
option(BUILD_APTX "Build aptX codec DLL" ON)
option(BUILD_LDAC "Build LDAC codec DLL" ON)
option(BUILD_LC3 "Build LC3 codec DLL" ON)
option(USE_STUB_BLUESPY "Use stub library for blueSPY (for CI builds)" OFF)

# --------- Global Settings ---------
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) 

# --------- Detect Build Mode ---------
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BLUESPY_CODECS_STANDALONE TRUE)
    message(STATUS "bluespy_codecs: Building STANDALONE")
else()
    set(BLUESPY_CODECS_STANDALONE FALSE)
    message(STATUS "bluespy_codecs: Building as SUBMODULE")
endif()

# --------- Setup blueSPY dependency ---------
if(BLUESPY_CODECS_STANDALONE)
    # STANDALONE: Handle include directory
    if(NOT DEFINED BLUESPY_INCLUDE_DIR)
        find_path(BLUESPY_INCLUDE_DIR 
            NAMES bluespy.h
            PATHS "C:/Program Files/RFcreations/blueSPY"
        )
    endif()
    
    if(NOT BLUESPY_INCLUDE_DIR)
        message(STATUS "bluespy_codecs: bluespy.h not found, using ci-stubs")
        set(BLUESPY_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ci-stubs/bluespy")
    endif()
    
    # STANDALONE: Handle library target
    if(NOT DEFINED LIB_BLUESPY_PATH)
        set(LIB_BLUESPY_PATH "C:/Path/to/Repos/libblueSPY/libblueSPY.lib")
    endif()
    
    if(USE_STUB_BLUESPY OR NOT EXISTS "${LIB_BLUESPY_PATH}")
        message(STATUS "bluespy_codecs: Building CI stub 'libblueSPY'")
        
        add_library(libblueSPY SHARED 
            ${CMAKE_CURRENT_SOURCE_DIR}/ci-stubs/bluespy/bluespy.cpp
        )
        
        if(WIN32)
            target_compile_definitions(libblueSPY PRIVATE BLUESPY_BUILD_DLL)
        else()
            target_compile_definitions(libblueSPY PRIVATE BLUESPY_API=)
        endif()
        
        set_target_properties(libblueSPY PROPERTIES OUTPUT_NAME "libblueSPY")
        set(LIB_BLUESPY_PATH libblueSPY)
    endif()
    
else()
    # SUBMODULE: Parent must provide everything
    if(NOT DEFINED BLUESPY_INCLUDE_DIR)
        message(FATAL_ERROR 
            "bluespy_codecs: BLUESPY_INCLUDE_DIR must be set by parent project"
        )
    endif()
    
    if(NOT DEFINED LIB_BLUESPY_PATH)
        message(FATAL_ERROR 
            "bluespy_codecs: LIB_BLUESPY_PATH must be set by parent project"
        )
    endif()
    
    if(NOT TARGET "${LIB_BLUESPY_PATH}")
        message(FATAL_ERROR 
            "bluespy_codecs: '${LIB_BLUESPY_PATH}' is not a valid target.\n"
            "Parent must define this target BEFORE adding bluespy_codecs via CPM."
        )
    endif()
endif()

message(STATUS "bluespy_codecs: BLUESPY_INCLUDE_DIR = ${BLUESPY_INCLUDE_DIR}")
message(STATUS "bluespy_codecs: LIB_BLUESPY_PATH = ${LIB_BLUESPY_PATH}")

include_directories(${BLUESPY_INCLUDE_DIR})

# --------- Interface libraries ---------
add_library(bluespy_codecs INTERFACE)
target_include_directories(bluespy_codecs INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_library(bluespy_codec_build INTERFACE)
target_link_libraries(bluespy_codec_build INTERFACE bluespy_codecs)
target_compile_definitions(bluespy_codec_build INTERFACE BLUESPY_CODEC_BUILD)

# --------- Codec builds ---------
if(BUILD_AAC)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    
    add_library(aac SHARED AAC.cpp)
    add_subdirectory(fdk-aac-stripped EXCLUDE_FROM_ALL)
    target_link_libraries(aac PRIVATE 
        fdk-aac 
        bluespy_codec_build 
        ${LIB_BLUESPY_PATH}
    )
endif()

if(BUILD_APTX)
    add_library(aptx SHARED
        APTX.cpp
        libfreeaptx/freeaptx.c
    )
    target_include_directories(aptx PRIVATE libfreeaptx)
    target_link_libraries(aptx PRIVATE 
        bluespy_codec_build 
        ${LIB_BLUESPY_PATH}
    )
endif()

if(BUILD_LDAC)
    add_library(ldac SHARED
        LDAC.cpp
        libldacdec/libldacdec.c
        libldacdec/bit_allocation.c
        libldacdec/huffCodes.c
        libldacdec/bit_reader.c
        libldacdec/utility.c
        libldacdec/imdct.c
        libldacdec/spectrum.c
    )
    target_include_directories(ldac PRIVATE libldacdec)
    target_link_libraries(ldac PRIVATE 
        bluespy_codec_build 
        ${LIB_BLUESPY_PATH}
    )
    if(UNIX)
        target_link_libraries(ldac PRIVATE m)
    endif()
endif()

if(BUILD_LC3)
    file(GLOB LIBLC3_SRC liblc3/src/*.c liblc3/src/*.s)

    add_library(liblc3 STATIC ${LIBLC3_SRC})
    target_include_directories(liblc3 PUBLIC liblc3/include)
    if(UNIX)
        target_link_libraries(liblc3 PRIVATE m)
    endif()

    add_library(lc3 SHARED LC3.cpp)
    target_include_directories(lc3 PRIVATE liblc3/include)
    target_link_libraries(lc3 PRIVATE
        bluespy_codec_build
        liblc3
        ${LIB_BLUESPY_PATH}
    )
    if(UNIX)
        target_link_libraries(lc3 PRIVATE m)
    endif()
endif()