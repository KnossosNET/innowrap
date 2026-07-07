cmake_minimum_required(VERSION 3.15)
project(innowrap CXX)

# ---------------------------------------------------------------------------
# You need:
#   * A checkout of innoextract (https://github.com/dscharrer/innoextract),
#     validated against commit 6e9e34ed0876014fdb46e684103ef8c3605e382e.
#   * Boost HEADERS only (no compiled boost libraries are required).
#
# Configure with:
#   cmake -B build -S native \
#         -DINNOEXTRACT_SRC=/path/to/innoextract/src \
#         -DBOOST_INCLUDEDIR=/path/to/boost \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED INNOEXTRACT_SRC)
    message(FATAL_ERROR "Set -DINNOEXTRACT_SRC=/path/to/innoextract/src")
endif()

# Normalize Windows-style backslash paths (D:\foo\bar) into CMake paths so they
# don't get misread as invalid string escapes (\D, \f, ...).
file(TO_CMAKE_PATH "${INNOEXTRACT_SRC}" INNOEXTRACT_SRC)
if(DEFINED BOOST_INCLUDEDIR)
    file(TO_CMAKE_PATH "${BOOST_INCLUDEDIR}" BOOST_INCLUDEDIR)
endif()

set(WRAPPER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/wrapper")

# Boost headers (header-only usage). If BOOST_INCLUDEDIR is given (e.g. for cross
# compiling, where find_package can't search the host), it is used verbatim.
if(DEFINED BOOST_INCLUDEDIR)
    set(Boost_INCLUDE_DIRS "${BOOST_INCLUDEDIR}")
else()
    find_package(Boost QUIET)
    if(NOT Boost_FOUND)
        message(WARNING "Boost headers not found; pass -DBOOST_INCLUDEDIR=/path/to/boost")
    endif()
endif()

# --- Source subset --------------------------------------------------------
# util: console/encoding/load/log only. Excluded: test.cpp, windows.cpp (pulls
#       <windows.h>+filesystem), time.cpp (boost::filesystem::last_write_time),
#       process.cpp (only needed to execute "run" entries; a parser never does).
set(INNO_UTIL
    util/console.cpp util/encoding.cpp util/load.cpp util/log.cpp)

# crypto: everything EXCEPT arc4.cpp / xchacha20.cpp (decryption is disabled;
#         GOG FreeSpace 2 installers are not encrypted).
set(INNO_CRYPTO
    crypto/adler32.cpp crypto/checksum.cpp crypto/crc32.cpp crypto/hasher.cpp
    crypto/md5.cpp crypto/pbkdf2.cpp crypto/sha1.cpp crypto/sha256.cpp)

set(INNO_LOADER loader/exereader.cpp loader/offsets.cpp)

# setup: all parser sources.
file(GLOB INNO_SETUP "${INNOEXTRACT_SRC}/setup/*.cpp")

# cli/goggalaxy.cpp: GOG Galaxy multi-part reconstruction (only header-only boost
#         + crypto/setup/util deps). Required so split .vp files are reassembled.
set(INNO_CLI cli/goggalaxy.cpp)

# stream: ONLY block.cpp, and we use OUR patched copy (chunk.cpp/file.cpp/
#         slice.cpp/lzma.cpp are NOT compiled — they pull compression libs).

set(INNO_SOURCES "")
foreach(f ${INNO_UTIL} ${INNO_CRYPTO} ${INNO_LOADER} ${INNO_CLI})
    list(APPEND INNO_SOURCES "${INNOEXTRACT_SRC}/${f}")
endforeach()
list(APPEND INNO_SOURCES ${INNO_SETUP})

# Our files (override block.cpp + release.cpp, add the wrapper + glue).
set(WRAPPER_SOURCES
    "${WRAPPER_SRC}/block.cpp"      # patched: delegates header inflate to C#
    "${WRAPPER_SRC}/release.cpp"
    "${WRAPPER_SRC}/extras.cpp"     # provides symbols from the .cpp we skip
    "${WRAPPER_SRC}/innowrap.cpp")  # the C ABI

add_library(innowrap SHARED ${INNO_SOURCES} ${WRAPPER_SOURCES})

target_include_directories(innowrap PRIVATE
    "${WRAPPER_SRC}"        # configure.hpp + innowrap_cb.hpp live here
    "${INNOEXTRACT_SRC}"
    ${Boost_INCLUDE_DIRS})

target_compile_definitions(innowrap PRIVATE BOOST_ALL_NO_LIB)

if(MSVC)
    target_compile_options(innowrap PRIVATE /O2 /EHsc)
    target_compile_definitions(innowrap PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(innowrap PRIVATE
        -O2 -fvisibility=hidden -ffunction-sections -fdata-sections)
    if(APPLE)
        target_link_options(innowrap PRIVATE -Wl,-dead_strip)
    else()
        target_link_options(innowrap PRIVATE -Wl,--gc-sections -s)
    endif()
endif()

if(WIN32)
    # innowrap.dll (no "lib" prefix) so DllImport("innowrap") resolves on Windows.
    set_target_properties(innowrap PROPERTIES PREFIX "" OUTPUT_NAME "innowrap")
else()
    # libinnowrap.so / libinnowrap.dylib — DllImport("innowrap") adds lib/.so itself.
    set_target_properties(innowrap PROPERTIES PREFIX "lib" OUTPUT_NAME "innowrap")
endif()
