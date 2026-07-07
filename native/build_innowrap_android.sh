#!/bin/sh
# Builds libinnowrap.so for every Android ABI: arm64-v8a, armeabi-v7a, x86, x86_64.
#
# The Android NDK is NOT downloaded by this script -- it is baked into the Docker
# image (see Dockerfile.innowrap-android) and honored here via $ANDROID_NDK_HOME.
# Running it directly requires ANDROID_NDK_HOME to point at a valid NDK (r25+).
#
# innowrap is a parser-only wrapper around innoextract: it links NO compression
# library, so no native compression toolchain is needed -- only the NDK C++
# toolchain and the (header-only) Boost headers (BOOST_INCLUDEDIR).
#
# The innoextract source is cloned fresh on every run; INNOEXTRACT_REF pins the ref.
#
# Outputs (loose .so per ABI, NO tar.gz):
#   prebuilt_android_innowrap/arm64-v8a/libinnowrap.so
#   prebuilt_android_innowrap/armeabi-v7a/libinnowrap.so
#   prebuilt_android_innowrap/x86/libinnowrap.so
#   prebuilt_android_innowrap/x86_64/libinnowrap.so

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

export ANDROID_PLATFORM="${ANDROID_PLATFORM:-android-24}"
INNOWRAP_SRC="$SCRIPT_DIR"                          # CMakeLists.txt + wrapper/
INNO_FORK="${INNO_FORK:-https://github.com/dscharrer/innoextract.git}"
INNO_REF="${INNO_REF:-6e9e34ed0876014fdb46e684103ef8c3605e382e}"
BOOST_INCLUDEDIR="${BOOST_INCLUDEDIR:-/usr/include}"
TARGET_PREBUILT_FOLDER="$SCRIPT_DIR/prebuilt_android_innowrap"
# Build OFF the (possibly bind-mounted) script dir. Windows Docker bind mounts
# report file mtimes that make ninja think build.ninja is perpetually dirty
# ("manifest still dirty ... perhaps system time is not set"). A container-local
# temp dir has sane timestamps; only the final .so is copied to the output folder.
TEMP_FOLDER="${INNOWRAP_BUILD_TMP:-/tmp/innowrap_build_android}"

if [ -z "$ANDROID_NDK_HOME" ] || [ ! -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" ]; then
    echo "Error: \$ANDROID_NDK_HOME is not set or does not point at a valid NDK." >&2
    echo "       It is expected to be baked into the Docker image (see" >&2
    echo "       Dockerfile.innowrap-android) or installed on the host." >&2
    exit 1
fi
echo "Using NDK at $ANDROID_NDK_HOME"

if [ ! -f "$INNOWRAP_SRC/CMakeLists.txt" ] || [ ! -d "$INNOWRAP_SRC/wrapper" ]; then
    echo "Error: expected the innowrap project at $INNOWRAP_SRC (CMakeLists.txt + wrapper/)." >&2
    exit 1
fi

rm -rf "$TARGET_PREBUILT_FOLDER" "$TEMP_FOLDER"
mkdir -p "$TARGET_PREBUILT_FOLDER" "$TEMP_FOLDER"

#############################################################################
# --- Get innoextract source (pinned) ---------------------------------------
#############################################################################
INNO_DIR="$TEMP_FOLDER/innoextract"
echo "Cloning innoextract (ref: $INNO_REF) ..."
git clone "$INNO_FORK" "$INNO_DIR" || { echo "git clone failed" >&2; exit 1; }
git -C "$INNO_DIR" checkout "$INNO_REF" || { echo "git checkout $INNO_REF failed" >&2; exit 1; }

# Copy the project (CMakeLists.txt + wrapper/) off the mount too, so NO configure
# input lives on the bind mount (cp gives the copies fresh, container-local mtimes).
PROJ_LOCAL="$TEMP_FOLDER/innowrap_src"
mkdir -p "$PROJ_LOCAL"
cp "$INNOWRAP_SRC/CMakeLists.txt" "$PROJ_LOCAL/"
cp -r "$INNOWRAP_SRC/wrapper" "$PROJ_LOCAL/"

#############################################################################
# Build helper: configures+builds libinnowrap for one ABI.
#############################################################################
build_one() {
    abi="$1"
    build_dir="$TEMP_FOLDER/build-$abi"

    echo ""
    echo "=== Building libinnowrap for Android/$abi ($ANDROID_PLATFORM) ==="
    cmake -S "$PROJ_LOCAL" -B "$build_dir" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM="$ANDROID_PLATFORM" \
        -DCMAKE_BUILD_TYPE=Release \
        -DINNOEXTRACT_SRC="$INNO_DIR/src" \
        -DBOOST_INCLUDEDIR="$BOOST_INCLUDEDIR"
    cmake --build "$build_dir" -j"$(nproc)"

    out_dir="$TARGET_PREBUILT_FOLDER/$abi"
    mkdir -p "$out_dir"
    cp "$build_dir"/libinnowrap.so "$out_dir"/ 2>/dev/null

    if [ ! -f "$out_dir/libinnowrap.so" ]; then
        echo "Error: libinnowrap.so was not produced for $abi" >&2
        exit 1
    fi
    echo "-> $out_dir/libinnowrap.so"
}

for abi in arm64-v8a armeabi-v7a x86 x86_64; do
    build_one "$abi"
done

#############################################################################
# Cleanup (keep per-ABI .so folders)
#############################################################################
rm -rf "$TEMP_FOLDER"

echo ""
echo "Done. Output in $TARGET_PREBUILT_FOLDER:"
find "$TARGET_PREBUILT_FOLDER" -name '*.so'
