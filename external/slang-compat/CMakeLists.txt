# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.10)
project(GeanySlangCompat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
foreach(option SLANG_INCLUDE_TESTS SLANG_INCLUDE_DOCS SLANG_INCLUDE_PYLIB SLANG_INCLUDE_INSTALL SLANG_INCLUDE_COVERAGE)
  set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
set(SLANG_MASTER_PROJECT OFF CACHE BOOL "" FORCE)
set(SLANG_INCLUDE_TOOLS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(GEANY_SLANG_COMPAT "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory("${GEANY_SLANG_SOURCE}" slang)
add_executable(insight_slang_probe probe.cpp)
target_link_libraries(insight_slang_probe PRIVATE slang::slang fmt::fmt)
enable_testing()
add_test(NAME slang_multifile
  COMMAND insight_slang_probe "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/leaf.sv" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/top.sv")

add_executable(insight_slang_charconv charconv-test.cpp)
target_compile_options(insight_slang_charconv PRIVATE -UNDEBUG)
add_test(NAME slang_integer_boundaries COMMAND insight_slang_charconv)

add_executable(insight-slang-semantic semantic.cpp)
target_link_libraries(insight-slang-semantic PRIVATE slang::slang fmt::fmt)
if(MINGW)
  # The editor supplies stdout and stderr pipes for this background service.
  set_property(TARGET insight-slang-semantic APPEND_STRING PROPERTY LINK_FLAGS " -mwindows")
endif()
add_test(NAME slang_semantic_protocol
  COMMAND "${PYTHON_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/semantic-test.py"
    "$<TARGET_FILE:insight-slang-semantic>" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
