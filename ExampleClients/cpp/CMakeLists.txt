# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# C++ example clients for the Tempo gRPC API. Mirrors the Rust and Python
# example clients but linked against the Tempo C++ wrapper library produced at
# Tempo prebuild time when TEMPO_GEN_CPP_API=1.

cmake_minimum_required(VERSION 3.20)
project(tempo_examples CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ----------------------------------------------------------------------------
# Locate the Tempo C++ API and the vendored gRPC.
# ----------------------------------------------------------------------------
set(TEMPO_PLUGIN_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../TempoCore" CACHE PATH
    "Path to the TempoCore plugin root (defaults to ../../TempoCore)")
set(TEMPO_API_DIR "${TEMPO_PLUGIN_ROOT}/Content/Cpp/API")
set(TEMPO_GRPC_DIR "${TEMPO_PLUGIN_ROOT}/Source/ThirdParty/gRPC")

if(APPLE)
    set(TEMPO_PLATFORM Mac)
elseif(WIN32)
    set(TEMPO_PLATFORM Win64)
elseif(UNIX)
    set(TEMPO_PLATFORM Linux)
else()
    message(FATAL_ERROR "Unsupported platform.")
endif()

if(NOT EXISTS "${TEMPO_API_DIR}/lib/${TEMPO_PLATFORM}")
    message(FATAL_ERROR
        "Tempo C++ API not built for ${TEMPO_PLATFORM}.\n"
        "  Expected: ${TEMPO_API_DIR}/lib/${TEMPO_PLATFORM}\n"
        "  Run a Tempo build with TEMPO_GEN_CPP_API=1 first.")
endif()
if(NOT EXISTS "${TEMPO_GRPC_DIR}/Includes")
    message(FATAL_ERROR "Vendored gRPC not found at ${TEMPO_GRPC_DIR}")
endif()

# ----------------------------------------------------------------------------
# tempo: imported static library produced by gen_cpp_api.py.
# ----------------------------------------------------------------------------
add_library(tempo STATIC IMPORTED GLOBAL)
if(WIN32)
    set_target_properties(tempo PROPERTIES
        IMPORTED_LOCATION "${TEMPO_API_DIR}/lib/${TEMPO_PLATFORM}/tempo.lib")
else()
    set_target_properties(tempo PROPERTIES
        IMPORTED_LOCATION "${TEMPO_API_DIR}/lib/${TEMPO_PLATFORM}/libtempo.a")
endif()
set_target_properties(tempo PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${TEMPO_API_DIR}/include;${TEMPO_API_DIR}/include/proto")
target_compile_definitions(tempo INTERFACE
    GOOGLE_PROTOBUF_NO_RTTI=1
    GRPC_ALLOW_EXCEPTIONS=0)
# The vendored "protobuf_tempo" was built with RTTI off and ships no typeinfo
# for its Message base classes — consumers must mirror that or get linker
# complaints about missing typeinfo for derived proto types.
target_compile_options(tempo INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-rtti>)

# ----------------------------------------------------------------------------
# grpc_vendored: link every .a/.lib in Libraries/<Platform>/. Mirrors what
# Tempo's gRPC.Build.cs does for Unreal modules; exports.def is a symbol
# re-export list, not a link list, and omits some upb_* archives that gRPC
# itself depends on (e.g., libupb_json_lib.a).
# ----------------------------------------------------------------------------
set(TEMPO_GRPC_LIB_DIR "${TEMPO_GRPC_DIR}/Libraries/${TEMPO_PLATFORM}")
if(WIN32)
    file(GLOB TEMPO_GRPC_LIBS "${TEMPO_GRPC_LIB_DIR}/*.lib")
else()
    file(GLOB TEMPO_GRPC_LIBS "${TEMPO_GRPC_LIB_DIR}/*.a")
endif()

add_library(grpc_vendored INTERFACE)
target_include_directories(grpc_vendored SYSTEM INTERFACE "${TEMPO_GRPC_DIR}/Includes")
target_link_libraries(grpc_vendored INTERFACE ${TEMPO_GRPC_LIBS})

# OpenSSL + zlib are required at link time. On macOS, point CMake at Homebrew's
# OpenSSL via -DOPENSSL_ROOT_DIR=$(brew --prefix openssl) if needed.
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
target_link_libraries(grpc_vendored INTERFACE OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB)

if(APPLE)
    find_library(SECURITY_FW Security)
    find_library(CF_FW CoreFoundation)
    target_link_libraries(grpc_vendored INTERFACE ${SECURITY_FW} ${CF_FW})
endif()

# ----------------------------------------------------------------------------
# Example executables
# ----------------------------------------------------------------------------
function(tempo_add_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE tempo grpc_vendored)
endfunction()

tempo_add_example(greeter_loop       GreeterLoop/greeter_loop.cc)
tempo_add_example(sensor_playground  SensorPlayground/sensor_playground.cc)
tempo_add_example(tempo_mcp          TempoMCP/tempo_mcp.cc)
