# SPDX-License-Identifier: GPL-2.0-or-later
# LLVM SDK only needs the interpreter; never pretend to find development files.
foreach(component IN LISTS Python3_FIND_COMPONENTS)
  if(NOT component STREQUAL "Interpreter")
    message(FATAL_ERROR "The CMake 3.10 SDK adapter only supports Python3 Interpreter")
  endif()
endforeach()
find_package(PythonInterp 3 REQUIRED)
set(Python3_EXECUTABLE "${PYTHON_EXECUTABLE}")
set(Python3_VERSION "${PYTHON_VERSION_STRING}")
set(Python3_Interpreter_FOUND TRUE)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Python3 REQUIRED_VARS Python3_EXECUTABLE
  VERSION_VAR Python3_VERSION)
