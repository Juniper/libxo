cmake_minimum_required(VERSION 3.13)
project(xo_validate C CXX)

# Tested against LLVM 14 (macOS), 21.x (FreeBSD 16), and 22.x (current).
# The clang plugin API used here has been stable across that range.

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

message(STATUS "Building xo_validate against LLVM ${LLVM_VERSION}")

# Root of the libxo source and build trees.
# Override with: cmake -DXO_SRCDIR=... -DXO_BUILDDIR=...
set(XO_SRCDIR   "${CMAKE_SOURCE_DIR}/../.."  CACHE PATH "libxo source root")
set(XO_BUILDDIR "${XO_SRCDIR}/build"      CACHE PATH "libxo build root")

find_library(XO_FORMAT_LIB xo_format
    PATHS "${XO_BUILDDIR}/format/.libs"
    NO_DEFAULT_PATH
    REQUIRED)

message(STATUS "xo_format library: ${XO_FORMAT_LIB}")

add_library(xo_validate MODULE xo_validate.cc xo_parse_shim.c)

# LLVM headers on some versions require macros like __STDC_CONSTANT_MACROS.
separate_arguments(LLVM_DEFS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(xo_validate PRIVATE ${LLVM_DEFS})

target_compile_options(xo_validate PRIVATE
    -fno-rtti
    -Wall
    -Wextra
)

target_include_directories(xo_validate SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)
target_include_directories(xo_validate PRIVATE
    "${XO_SRCDIR}/libxo"
    "${XO_BUILDDIR}/libxo"
    "${CMAKE_SOURCE_DIR}"
)

target_link_libraries(xo_validate PRIVATE ${XO_FORMAT_LIB})

# Clang symbols are provided by the host process at runtime; don't require
# them to be resolved at link time.  macOS requires an explicit flag for this;
# on ELF platforms (Linux, FreeBSD) it is the default for shared objects.
if(APPLE)
    target_link_options(xo_validate PRIVATE -undefined dynamic_lookup)
endif()

# Suppress the .dylib/.so suffix that cmake adds to MODULE targets on some
# platforms — clang's -fplugin= accepts the path as-is.
set_target_properties(xo_validate PROPERTIES PREFIX "")

install(TARGETS xo_validate DESTINATION lib/libxo)
