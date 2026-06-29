cmake_minimum_required(VERSION 3.18)

project(obj_sequence_to_alembic LANGUAGES CXX)

if(NOT APPLE)
  message(FATAL_ERROR "This CMake build is intended for macOS. Windows users can keep using the Visual Studio solution.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${REPO_ROOT}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${REPO_ROOT}/bin")
endforeach()

set(HOMEBREW_PREFIXES)
if(DEFINED ENV{HOMEBREW_PREFIX} AND EXISTS "$ENV{HOMEBREW_PREFIX}")
  list(APPEND HOMEBREW_PREFIXES "$ENV{HOMEBREW_PREFIX}")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  list(APPEND HOMEBREW_PREFIXES "/opt/homebrew" "/usr/local")
else()
  list(APPEND HOMEBREW_PREFIXES "/usr/local" "/opt/homebrew")
endif()

set(HOMEBREW_PREFIX "")
foreach(prefix IN LISTS HOMEBREW_PREFIXES)
  if(EXISTS "${prefix}")
    set(HOMEBREW_PREFIX "${prefix}")
    break()
  endif()
endforeach()

if(NOT HOMEBREW_PREFIX)
  message(FATAL_ERROR "Homebrew was not found. Install Homebrew, then run: brew install alembic")
endif()

message(STATUS "Using Homebrew prefix: ${HOMEBREW_PREFIX}")

list(APPEND CMAKE_PREFIX_PATH
  "${HOMEBREW_PREFIX}"
  "${HOMEBREW_PREFIX}/opt/alembic"
  "${HOMEBREW_PREFIX}/opt/hdf5"
  "${HOMEBREW_PREFIX}/opt/imath"
  "${HOMEBREW_PREFIX}/opt/ilmbase"
  "${HOMEBREW_PREFIX}/opt/zlib"
)

find_package(Alembic CONFIG QUIET)
find_package(HDF5 COMPONENTS C HL QUIET)
find_package(Imath CONFIG QUIET)
find_path(ALEMBIC_INCLUDE_DIR
  NAMES Alembic/AbcGeom/All.h
  PATHS "${HOMEBREW_PREFIX}/opt/alembic/include" "${HOMEBREW_PREFIX}/include"
  NO_DEFAULT_PATH
)
find_library(ALEMBIC_LIBRARY
  NAMES Alembic alembic
  PATHS "${HOMEBREW_PREFIX}/opt/alembic/lib" "${HOMEBREW_PREFIX}/lib"
  NO_DEFAULT_PATH
)

find_path(IMATH_INCLUDE_DIR
  NAMES Imath/ImathVec.h OpenEXR/ImathVec.h ImathVec.h
  PATHS
    "${HOMEBREW_PREFIX}/opt/imath/include"
    "${HOMEBREW_PREFIX}/opt/imath/include/Imath"
    "${HOMEBREW_PREFIX}/opt/ilmbase/include"
    "${HOMEBREW_PREFIX}/include"
  NO_DEFAULT_PATH
)
find_library(IMATH_LIBRARY
  NAMES Imath Half IlmImf IlmThread Iex
  PATHS "${HOMEBREW_PREFIX}/opt/imath/lib" "${HOMEBREW_PREFIX}/opt/ilmbase/lib" "${HOMEBREW_PREFIX}/lib"
  NO_DEFAULT_PATH
)

find_path(HOMEBREW_ZLIB_INCLUDE_DIR
  NAMES zlib.h
  PATHS "${HOMEBREW_PREFIX}/opt/zlib/include" "${HOMEBREW_PREFIX}/include"
  NO_DEFAULT_PATH
)
find_library(HOMEBREW_ZLIB_LIBRARY
  NAMES z
  PATHS "${HOMEBREW_PREFIX}/opt/zlib/lib" "${HOMEBREW_PREFIX}/lib"
  NO_DEFAULT_PATH
)

if(NOT Alembic_FOUND AND (NOT ALEMBIC_INCLUDE_DIR OR NOT ALEMBIC_LIBRARY))
  message(FATAL_ERROR "Alembic was not found in Homebrew. Run: brew install alembic")
endif()

if(NOT HDF5_FOUND)
  message(FATAL_ERROR "HDF5 was not found in Homebrew. Run: brew install hdf5")
endif()

if(NOT Imath_FOUND AND (NOT IMATH_INCLUDE_DIR OR NOT IMATH_LIBRARY))
  message(FATAL_ERROR "Imath/IlmBase was not found in Homebrew. Run: brew install imath")
endif()

if(NOT HOMEBREW_ZLIB_INCLUDE_DIR OR NOT HOMEBREW_ZLIB_LIBRARY)
  message(FATAL_ERROR "zlib was not found in Homebrew. Run: brew install zlib")
endif()

add_library(Homebrew::ZLIB UNKNOWN IMPORTED)
set_target_properties(Homebrew::ZLIB PROPERTIES
  IMPORTED_LOCATION "${HOMEBREW_ZLIB_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${HOMEBREW_ZLIB_INCLUDE_DIR}"
)

set(COMPAT_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/compat")
file(MAKE_DIRECTORY "${COMPAT_INCLUDE_DIR}")

file(WRITE "${COMPAT_INCLUDE_DIR}/Vector" "#include <vector>\n")

file(WRITE "${COMPAT_INCLUDE_DIR}/io.h" [=[
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <sys/stat.h>
#include <vector>

#ifndef _A_SUBDIR
#define _A_SUBDIR 0x10
#endif

struct _finddata_t
{
    unsigned attrib;
    char name[1024];
};

namespace objs2abc_compat
{
    struct FindHandle
    {
        DIR* dir;
        std::string directory;
    };

    inline std::string normalize_path(const std::string& input)
    {
        std::string path = input;
        for (char& c : path)
        {
            if (c == '\\')
            {
                c = '/';
            }
        }
        return path;
    }

    inline std::string directory_from_pattern(const char* pattern)
    {
        std::string directory = normalize_path(pattern ? pattern : "");
        if (directory.size() >= 2 && directory.compare(directory.size() - 2, 2, "/*") == 0)
        {
            directory.erase(directory.size() - 2);
        }
        while (directory.size() > 1 && directory.back() == '/')
        {
            directory.pop_back();
        }
        return directory.empty() ? "." : directory;
    }

    inline int fill_find_data(FindHandle* handle, _finddata_t* out)
    {
        if (!handle || !handle->dir || !out)
        {
            return -1;
        }

        dirent* entry = nullptr;
        while ((entry = readdir(handle->dir)) != nullptr)
        {
            std::string full_path = handle->directory + "/" + entry->d_name;
            struct stat st;
            bool is_directory = stat(full_path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
            std::string name = entry->d_name;

            if (!is_directory)
            {
                std::string lower = name;
                std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".obj") != 0)
                {
                    continue;
                }
            }

            std::snprintf(out->name, sizeof(out->name), "%s", entry->d_name);
            out->attrib = 0;

            if (is_directory)
            {
                out->attrib |= _A_SUBDIR;
            }
            return 0;
        }

        return -1;
    }
}

inline intptr_t _findfirst(const char* pattern, _finddata_t* out)
{
    std::string directory = objs2abc_compat::directory_from_pattern(pattern);
    DIR* dir = opendir(directory.c_str());
    if (!dir)
    {
        return -1;
    }

    auto* handle = new objs2abc_compat::FindHandle{dir, directory};
    if (objs2abc_compat::fill_find_data(handle, out) != 0)
    {
        closedir(dir);
        delete handle;
        return -1;
    }

    return reinterpret_cast<intptr_t>(handle);
}

inline int _findnext(intptr_t id, _finddata_t* out)
{
    return objs2abc_compat::fill_find_data(reinterpret_cast<objs2abc_compat::FindHandle*>(id), out);
}

inline int _findclose(intptr_t id)
{
    auto* handle = reinterpret_cast<objs2abc_compat::FindHandle*>(id);
    if (!handle)
    {
        return -1;
    }

    int result = closedir(handle->dir);
    delete handle;
    return result;
}

#define open(path_expr) open(::objs2abc_compat::normalize_path(path_expr).c_str())
]=])

add_executable(Objs2Abc
  Objs2Abc/Objs2Abc.cpp
)

target_compile_definitions(Objs2Abc PRIVATE sscanf_s=sscanf)
target_compile_options(Objs2Abc PRIVATE
  -Wall
  -Wextra
  -Wno-nonportable-include-path
  -Wno-sign-compare
  -Wno-unused-parameter
  -Wno-unused-variable
)

target_compile_options(Objs2Abc PRIVATE "-idirafter" "${COMPAT_INCLUDE_DIR}")

if(Alembic_FOUND AND TARGET Alembic::Alembic)
  target_link_libraries(Objs2Abc PRIVATE Alembic::Alembic)
elseif(Alembic_FOUND AND TARGET Alembic::AlembicShared)
  target_link_libraries(Objs2Abc PRIVATE Alembic::AlembicShared)
else()
  target_include_directories(Objs2Abc PRIVATE "${ALEMBIC_INCLUDE_DIR}")
  target_link_libraries(Objs2Abc PRIVATE "${ALEMBIC_LIBRARY}")
endif()

if(Imath_FOUND)
  if(TARGET Imath::Imath)
    target_link_libraries(Objs2Abc PRIVATE Imath::Imath)
  elseif(TARGET Imath::Half)
    target_link_libraries(Objs2Abc PRIVATE Imath::Half)
  endif()
else()
  target_include_directories(Objs2Abc PRIVATE "${IMATH_INCLUDE_DIR}")
  target_link_libraries(Objs2Abc PRIVATE "${IMATH_LIBRARY}")
endif()

if(TARGET HDF5::HDF5)
  target_link_libraries(Objs2Abc PRIVATE HDF5::HDF5)
else()
  target_include_directories(Objs2Abc PRIVATE ${HDF5_INCLUDE_DIRS})
  target_link_libraries(Objs2Abc PRIVATE ${HDF5_LIBRARIES} ${HDF5_HL_LIBRARIES})
endif()

target_link_libraries(Objs2Abc PRIVATE Homebrew::ZLIB)

if(ALEMBIC_INCLUDE_DIR)
  target_include_directories(Objs2Abc PRIVATE "${ALEMBIC_INCLUDE_DIR}")
endif()
if(IMATH_INCLUDE_DIR)
  target_include_directories(Objs2Abc PRIVATE "${IMATH_INCLUDE_DIR}")
endif()

set_target_properties(Objs2Abc PROPERTIES
  OUTPUT_NAME "Objs2Abc"
  RUNTIME_OUTPUT_DIRECTORY "${REPO_ROOT}/bin"
)
