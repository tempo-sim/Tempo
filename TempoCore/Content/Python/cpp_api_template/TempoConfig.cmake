# Copyright Tempo Simulation, LLC. All Rights Reserved
#
# The Tempo C++ client, as a CMake package:
#
#   find_package(Tempo CONFIG REQUIRED PATHS <this directory>)
#   target_link_libraries(my_client PRIVATE tempo::reflected)   # the reflected API, and everything
#   target_link_libraries(my_client PRIVATE tempo::tempo)       # the RPC client alone
#
# Works from either place the client lives: Packaged/API/Cpp, which carries the gRPC headers and the
# shared tempogrpc library beside it under third_party/ and lib/, and the build tree it is generated
# into, TempoCore/Content/Cpp/API, which takes them from the vendored gRPC next door.
#
# gRPC, protobuf and abseil come from the one shared library, tempogrpc, never the static archives
# beside it, for the reason Tempo's own modules link it that way: one copy of their global state.
# It carries OpenSSL and zlib inside it. On Windows, copy tempogrpc.dll next to the executable.

get_filename_component(_TEMPO_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

if(APPLE)
    set(_TEMPO_PLATFORM Mac)
elseif(WIN32)
    set(_TEMPO_PLATFORM Win64)
else()
    set(_TEMPO_PLATFORM Linux)
endif()
set(_TEMPO_LIB "${_TEMPO_ROOT}/lib/${_TEMPO_PLATFORM}")

if(EXISTS "${_TEMPO_ROOT}/third_party/grpc/include")
    set(_TEMPO_GRPC_INCLUDE "${_TEMPO_ROOT}/third_party/grpc/include")
    set(_TEMPO_GRPC_LIB "${_TEMPO_LIB}")
    set(_TEMPO_GRPC_BIN "${_TEMPO_LIB}")
else()
    set(_TEMPO_GRPC "${_TEMPO_ROOT}/../../../Source/ThirdParty/gRPC")
    set(_TEMPO_GRPC_INCLUDE "${_TEMPO_GRPC}/Includes")
    # The vendored gRPC calls Windows "Windows", where the C++ client's own folder is "Win64".
    if(WIN32)
        set(_TEMPO_GRPC_LIB "${_TEMPO_GRPC}/Libraries/Windows")
    else()
        set(_TEMPO_GRPC_LIB "${_TEMPO_GRPC}/Libraries/${_TEMPO_PLATFORM}")
    endif()
    set(_TEMPO_GRPC_BIN "${_TEMPO_GRPC}/Binaries/Windows")
endif()

if(NOT TARGET tempo::grpc)
    add_library(tempo::grpc SHARED IMPORTED)
    if(WIN32)
        set_target_properties(tempo::grpc PROPERTIES
            IMPORTED_LOCATION "${_TEMPO_GRPC_BIN}/tempogrpc.dll"
            IMPORTED_IMPLIB "${_TEMPO_GRPC_LIB}/tempogrpc.lib")
    elseif(APPLE)
        set_target_properties(tempo::grpc PROPERTIES
            IMPORTED_LOCATION "${_TEMPO_GRPC_LIB}/libtempogrpc.dylib"
            IMPORTED_SONAME "@rpath/libtempogrpc.dylib")
    else()
        # TempoThirdParty links it with a plain add_library(SHARED) and no version
        # (Utils/tempogrpc/CMakeLists.txt), for which CMake's soname is the file name -- as the Mac
        # library's @rpath install name above is CMake's default for the same target.
        set_target_properties(tempo::grpc PROPERTIES
            IMPORTED_LOCATION "${_TEMPO_GRPC_LIB}/libtempogrpc.so"
            IMPORTED_SONAME "libtempogrpc.so")
    endif()
    set_target_properties(tempo::grpc PROPERTIES
        INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_TEMPO_GRPC_INCLUDE}"
        INTERFACE_INCLUDE_DIRECTORIES "${_TEMPO_GRPC_INCLUDE}")
endif()

if(NOT TARGET tempo::tempo)
    add_library(tempo::tempo STATIC IMPORTED)
    if(WIN32)
        set(_TEMPO_ARCHIVE "${_TEMPO_LIB}/tempo.lib")
    else()
        set(_TEMPO_ARCHIVE "${_TEMPO_LIB}/libtempo.a")
    endif()
    set_target_properties(tempo::tempo PROPERTIES
        IMPORTED_LOCATION "${_TEMPO_ARCHIVE}"
        INTERFACE_INCLUDE_DIRECTORIES "${_TEMPO_ROOT}/include;${_TEMPO_ROOT}/include/proto"
        # The defines libtempo was built with: the vendored gRPC's, and those of a DLL consumer.
        INTERFACE_COMPILE_DEFINITIONS "GOOGLE_PROTOBUF_NO_RTTI=1;GPR_FORBID_UNREACHABLE_CODE=1;GRPC_ALLOW_EXCEPTIONS=0;PROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0;GOOGLE_PROTOBUF_INTERNAL_DONATE_STEAL_INLINE=0;PROTOBUF_USE_DLLS;ABSL_CONSUME_DLL"
        # The vendored protobuf ships no typeinfo for its message base classes, so every
        # translation unit that includes a generated header compiles without RTTI.
        INTERFACE_COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/GR-,-fno-rtti>"
        INTERFACE_COMPILE_FEATURES cxx_std_20
        INTERFACE_LINK_LIBRARIES tempo::grpc)
endif()

if(NOT TARGET tempo::reflected)
    if(WIN32)
        set(_TEMPO_REFLECTED "${_TEMPO_LIB}/tempo_reflected.lib")
    else()
        set(_TEMPO_REFLECTED "${_TEMPO_LIB}/libtempo_reflected.a")
    endif()
    # The reflected API is built at package time, so a client generated only at build time has
    # none: tempo::reflected is then simply not there.
    if(EXISTS "${_TEMPO_REFLECTED}")
        add_library(tempo::reflected STATIC IMPORTED)
        set_target_properties(tempo::reflected PROPERTIES
            IMPORTED_LOCATION "${_TEMPO_REFLECTED}"
            INTERFACE_LINK_LIBRARIES tempo::tempo)
    endif()
endif()

set(Tempo_FOUND TRUE)
