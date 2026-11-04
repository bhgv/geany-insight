# SPDX-License-Identifier: GPL-2.0-or-later
# Explicit, pinned local dependencies; no FetchContent or network in CMake.
foreach(name fmt unordered_dense filesystem)
  if(NOT EXISTS "${GEANY_SLANG_DEPS}/${name}/.git")
    message(FATAL_ERROR "Missing prepared slang dependency: ${name}; run build_external_slang.py")
  endif()
endforeach()
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${GEANY_SLANG_DEPS}/fmt" "${CMAKE_BINARY_DIR}/compat-fmt")
set_target_properties(fmt PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(unordered_dense INTERFACE)
add_library(unordered_dense::unordered_dense ALIAS unordered_dense)
target_include_directories(unordered_dense INTERFACE "${GEANY_SLANG_DEPS}/unordered_dense/include")
# ghc::filesystem keeps the same API on GCC 7, where std::filesystem is missing.
add_library(geany_slang_filesystem INTERFACE)
target_include_directories(geany_slang_filesystem SYSTEM INTERFACE "${GEANY_SLANG_DEPS}/filesystem/include")
