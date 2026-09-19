#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

# Links Tempo's vendored gRPC, Protobuf and Abseil static libraries into one shared library.
#
# Those libraries keep global state (Protobuf's descriptor pool, gRPC's core, Abseil's flags), so a
# process must contain exactly one copy of them, which every Tempo module then shares. A shared
# library holding all of them is that one copy. It is linked here, rather than shipped prebuilt,
# because it takes OpenSSL and zlib from the engine it will run in.
#
# Usage: LinkGrpcShared.sh <EngineDir> <TempoCore plugin dir> <TargetPlatform>

set -e

ENGINE_DIR="${1//\\//}"
PLUGIN_DIR="${2//\\//}"
TARGET_PLATFORM="$3"

# Only Mac links the shared library so far. Elsewhere Tempo's UnrealBuildTool toolchains re-export
# the static libraries from TempoCore instead.
if [ "$TARGET_PLATFORM" != "Mac" ]; then
  exit 0
fi

LIBRARIES_DIR="$PLUGIN_DIR/Source/ThirdParty/gRPC/Libraries/Mac"
OUTPUT_DIR="$PLUGIN_DIR/Binaries/ThirdParty/gRPC/Mac"
OUTPUT="$OUTPUT_DIR/libtempogrpc.dylib"

INCLUDES_DIR="$PLUGIN_DIR/Source/ThirdParty/gRPC/Includes"
# gRPC's C++ server API is built with hidden visibility, so the shared library cannot export it.
# TempoCore's few uses of it are compiled into the shared library instead.
SERVER_SOURCE_DIR="$PLUGIN_DIR/Source/TempoCore/Private"
SERVER_SOURCE="$SERVER_SOURCE_DIR/TempoGrpcServer.cpp"

OPENSSL_DIR=$(find "$ENGINE_DIR/Source/ThirdParty/OpenSSL" -type d -path "*/lib/Mac" | sort | tail -1)
ZLIB=$(find "$ENGINE_DIR/Source/ThirdParty/zlib" -name "libz.a" -path "*/Mac/*" | sort | tail -1)
if [ ! -d "$LIBRARIES_DIR" ] || [ -z "$OPENSSL_DIR" ] || [ -z "$ZLIB" ]; then
  echo "LinkGrpcShared.sh: could not find the gRPC libraries ($LIBRARIES_DIR), or the engine's OpenSSL or zlib. Run Scripts/SyncDeps.sh." >&2
  exit 1
fi

# Up to date if nothing that goes into it is newer.
if [ -f "$OUTPUT" ] && [ -z "$(find "$LIBRARIES_DIR" "$OPENSSL_DIR" "$ZLIB" "$SERVER_SOURCE" "$SERVER_SOURCE_DIR/TempoGrpcServer.h" "${BASH_SOURCE[0]}" -newer "$OUTPUT" -print -quit)" ]; then
  exit 0
fi

echo "[Tempo Prebuild] Linking $OUTPUT"

# exports.def names the libraries to take whole, whether or not anything refers to them. A few are
# left out because they repeat symbols of the others, and are only searched for what is missing.
LINK_ARGS=()
for LIBRARY in "$LIBRARIES_DIR"/*.a; do
  if grep -qx "$(basename "$LIBRARY")" "$LIBRARIES_DIR/exports.def"; then
    LINK_ARGS+=("-Wl,-force_load,$LIBRARY")
  else
    LINK_ARGS+=("$LIBRARY")
  fi
done

# The libraries' own deployment target, so the linker neither warns nor raises it.
MIN_OS=$(otool -l -arch arm64 "$LIBRARIES_DIR/libprotobuf.a" | awk '/minos/ { print $2; exit }')

mkdir -p "$OUTPUT_DIR"

# The definitions are those gRPC.Build.cs and TempoCore.Build.cs give every module that uses gRPC.
xcrun clang++ -c -std=c++17 -O2 -arch arm64 -arch x86_64 "-mmacosx-version-min=${MIN_OS:-11.0}" -fvisibility=hidden \
  -DNDEBUG -DTEMPO_GRPC_SERVER_IN_SHARED_LIBRARY=1 \
  -DGOOGLE_PROTOBUF_NO_RTTI=1 -DGPR_FORBID_UNREACHABLE_CODE=1 -DGRPC_ALLOW_EXCEPTIONS=0 \
  -DPROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0 -DGOOGLE_PROTOBUF_INTERNAL_DONATE_STEAL_INLINE=0 \
  -DABSL_BUILD_DLL=1 -DPROTOBUF_USE_DLLS=1 \
  -I "$INCLUDES_DIR" -I "$SERVER_SOURCE_DIR" \
  "$SERVER_SOURCE" -o "$OUTPUT_DIR/TempoGrpcServer.o"

xcrun clang++ -dynamiclib -arch arm64 -arch x86_64 "-mmacosx-version-min=${MIN_OS:-11.0}" \
  -install_name "@rpath/libtempogrpc.dylib" \
  -o "$OUTPUT.tmp" \
  "$OUTPUT_DIR/TempoGrpcServer.o" \
  "${LINK_ARGS[@]}" \
  "$OPENSSL_DIR/libssl.a" "$OPENSSL_DIR/libcrypto.a" "$ZLIB" \
  -framework CoreFoundation -framework Security -lresolv
mv "$OUTPUT.tmp" "$OUTPUT"
