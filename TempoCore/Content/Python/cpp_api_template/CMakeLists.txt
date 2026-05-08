# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# Internal CMakeLists.txt invoked by gen_cpp_api.py at Tempo prebuild time.
# Compiles the Tempo C++ wrapper + protoc-generated stubs into a single static
# library. Headers from the vendored gRPC are used at compile time; gRPC itself
# is NOT linked here — the consumer of libtempo links their own gRPC.

cmake_minimum_required(VERSION 3.20)
project(tempo CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED TEMPO_GRPC_DIR)
    message(FATAL_ERROR "TEMPO_GRPC_DIR must be set (path to TempoCore/Source/ThirdParty/gRPC)")
endif()

if(NOT EXISTS "${TEMPO_GRPC_DIR}/Includes")
    message(FATAL_ERROR "TEMPO_GRPC_DIR=${TEMPO_GRPC_DIR} does not contain an Includes/ directory")
endif()

file(GLOB_RECURSE TEMPO_WRAPPER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc")
file(GLOB_RECURSE TEMPO_PROTO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/proto_gen/*.cc")

if(NOT TEMPO_PROTO_SOURCES)
    message(FATAL_ERROR "No protoc-generated .cc files found in proto_gen/. Did gen_cpp_api.py run protoc?")
endif()

add_library(tempo STATIC ${TEMPO_WRAPPER_SOURCES} ${TEMPO_PROTO_SOURCES})

target_include_directories(tempo PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/proto_gen")
target_include_directories(tempo SYSTEM PUBLIC
    "${TEMPO_GRPC_DIR}/Includes")

# Match the defines used to build the vendored gRPC. ABI compatibility with
# linked gRPC depends on these matching at consumer build time too.
target_compile_definitions(tempo PUBLIC
    GOOGLE_PROTOBUF_NO_RTTI=1
    GPR_FORBID_UNREACHABLE_CODE=1
    GRPC_ALLOW_EXCEPTIONS=0
    PROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0
    GOOGLE_PROTOBUF_INTERNAL_DONATE_STEAL_INLINE=0)

if(MSVC)
    # /GR- disables RTTI, matching how the vendored protobuf was built. The
    # rebadged "protobuf_tempo" base classes don't ship typeinfo, so any TU
    # that compiles against their headers must do the same.
    target_compile_options(tempo PRIVATE /GR- /W3 /wd4146 /wd4244 /wd4251 /wd4267 /wd4800)
else()
    target_compile_options(tempo PRIVATE
        -fno-rtti
        -Wno-deprecated-declarations
        -Wno-unused-parameter
        -Wno-unused-function)
endif()

# Keep the archive output simple: lib/<platform>/libtempo.a (or tempo.lib on Windows).
set_target_properties(tempo PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
