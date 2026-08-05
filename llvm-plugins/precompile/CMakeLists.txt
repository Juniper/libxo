cmake_minimum_required(VERSION 3.13)
project(xo_precompile CXX C)

# LLVM New-PM module pass: rewrites xo_emit() calls with constant format
# strings into xo_emit_cached() calls backed by pre-parsed field tables.
# Load at compile time with:  clang -fpass-plugin=/path/to/xo_precompile.so

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Building xo_precompile against LLVM ${LLVM_VERSION}")

# Root of the libxo source and build trees.
# Override with: cmake -DXO_SRCDIR=... -DXO_BUILDDIR=...
set(XO_SRCDIR   "${CMAKE_SOURCE_DIR}/../.."  CACHE PATH "libxo source root")
set(XO_BUILDDIR "${XO_SRCDIR}/build"      CACHE PATH "libxo build root")

find_library(XO_FORMAT_LIB xo_format
    PATHS "${XO_BUILDDIR}/format/.libs"
    NO_DEFAULT_PATH
    REQUIRED)

message(STATUS "xo_format library: ${XO_FORMAT_LIB}")

# xo_parse_shim.c lives beside the validate plugin; include it here so the
# precompile pass gets xo_shim_parse_fields without duplicating source.
add_library(xo_precompile MODULE
    xo_precompile.cc
    ../validate/xo_parse_shim.c)

separate_arguments(LLVM_DEFS NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_definitions(xo_precompile PRIVATE ${LLVM_DEFS})

target_compile_options(xo_precompile PRIVATE
    -fno-rtti
    -Wall
    -Wextra
)

target_include_directories(xo_precompile SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
)
target_include_directories(xo_precompile PRIVATE
    "${XO_SRCDIR}/libxo"
    "${XO_BUILDDIR}/libxo"
    "${CMAKE_SOURCE_DIR}/../validate"
)

target_link_libraries(xo_precompile PRIVATE ${XO_FORMAT_LIB})

# On macOS, LLVM symbols are provided by the host process at runtime.
if(APPLE)
    target_link_options(xo_precompile PRIVATE -undefined dynamic_lookup)
endif()

# Drop the .dylib/.so prefix so -fpass-plugin= can use the path as-is.
set_target_properties(xo_precompile PROPERTIES PREFIX "")

install(TARGETS xo_precompile DESTINATION lib/libxo)
