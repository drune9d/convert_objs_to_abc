cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

project(obj_sequence_to_alembic_windows LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "This Windows package is intended to be configured on Windows.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${REPO_ROOT}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config} "${REPO_ROOT}/bin")
endforeach()

find_package(Imath CONFIG QUIET)
find_package(hdf5 CONFIG QUIET)
find_package(ZLIB QUIET)
find_package(Alembic CONFIG REQUIRED)

add_executable(Objs2Abc
  Objs2Abc/Objs2Abc.cpp
)

target_compile_definitions(Objs2Abc PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
  set_property(TARGET Objs2Abc PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  target_compile_options(Objs2Abc PRIVATE
    /permissive-
    /EHsc
    /MP
    /W4
    /wd4100
    /wd4244
    /wd4267
  )
else()
  target_compile_options(Objs2Abc PRIVATE
    -Wall
    -Wextra
    -Wno-sign-compare
    -Wno-unused-parameter
    -Wno-unused-variable
  )
endif()

if(TARGET Alembic::Alembic)
  target_link_libraries(Objs2Abc PRIVATE Alembic::Alembic)
elseif(TARGET Alembic::AlembicShared)
  target_link_libraries(Objs2Abc PRIVATE Alembic::AlembicShared)
elseif(TARGET Alembic::alembic)
  target_link_libraries(Objs2Abc PRIVATE Alembic::alembic)
elseif(TARGET alembic)
  target_link_libraries(Objs2Abc PRIVATE alembic)
else()
  message(FATAL_ERROR "Alembic was found, but no known Alembic CMake target was exported.")
endif()

set(OPTIONAL_DEP_TARGETS
  Imath::Imath
  Imath::Half
  ZLIB::ZLIB
  hdf5::hdf5-static
  hdf5::hdf5
)

foreach(dep IN LISTS OPTIONAL_DEP_TARGETS)
  if(TARGET ${dep})
    target_link_libraries(Objs2Abc PRIVATE ${dep})
  endif()
endforeach()

set_target_properties(Objs2Abc PROPERTIES
  OUTPUT_NAME "Objs2Abc"
  RUNTIME_OUTPUT_DIRECTORY "${REPO_ROOT}/bin"
  VS_DEBUGGER_WORKING_DIRECTORY "${REPO_ROOT}"
)
