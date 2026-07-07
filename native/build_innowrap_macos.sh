#!/bin/sh
# Builds libinnowrap.dylib for macOS: Apple Silicon + x86_64.
#
# innowrap is a parser-only wrapper around innoextract. It links NO compression
# library, so cross-compiling needs nothing special beyond the C++ toolchain and
# the (header-only) Boost headers (libboost-dev).
#
# The innoextract source is cloned fresh on every run; INNOEXTRACT_REF pins the ref.
#
# Outputs:
#   prebuilt_macos_innowrap/universal/libinnowrap.dylib

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# The CMake project (CMakeLists.txt + wrapper/) lives next to this script.
INNOWRAP_SRC="$SCRIPT_DIR"
INNO_FORK="${INNO_FORK:-https://github.com/dscharrer/innoextract.git}"
INNO_REF="${INNO_REF:-6e9e34ed0876014fdb46e684103ef8c3605e382e}"
BOOST_INCLUDEDIR="${BOOST_INCLUDEDIR:-"$(brew --prefix boost)/include"}"
TARGET_PREBUILT_FOLDER="$SCRIPT_DIR/prebuilt_macos_innowrap"
# Build OFF the (possibly bind-mounted) script dir. Windows Docker bind mounts
# report file mtimes that make ninja think build.ninja is perpetually dirty
# ("manifest still dirty ... perhaps system time is not set"). A container-local
# temp dir has sane timestamps; only the final .so is copied to the output folder.
TEMP_FOLDER="${INNOWRAP_BUILD_TMP:-/tmp/innowrap_build_macos}"

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
# Build helper: configures+builds libinnowrap for one (arch, toolchain).
#   $1 = label (x64 / arm64 / riscv64)
#   $2 = CMAKE_SYSTEM_PROCESSOR (empty = native)
#   $3 = CC  (empty = native)
#   $4 = CXX (empty = native)
#############################################################################
build() {
    label="universal"
    build_dir="$TEMP_FOLDER/build-$label"

    echo ""
    echo "=== Building libinnowrap for macOS ==="

    # shellcheck disable=SC2086
    cmake -S "$PROJ_LOCAL" -B "$build_dir" -G Ninja \
        -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0" \
        -DCMAKE_BUILD_TYPE=Release \
        -DINNOEXTRACT_SRC="$INNO_DIR/src" \
        -DBOOST_INCLUDEDIR="$BOOST_INCLUDEDIR"
    cmake --build "$build_dir" -j"$(nproc)"

    out_dir="$TARGET_PREBUILT_FOLDER/$label"
    mkdir -p "$out_dir"
    cp "$build_dir"/libinnowrap.dylib "$out_dir"/ 2>/dev/null

    if [ ! -f "$out_dir/libinnowrap.dylib" ]; then
        echo "Error: libinnowrap.dylib was not produced for $label" >&2
        exit 1
    fi

    codesign --force --timestamp --deep --sign - "$out_dir/libinnowrap.dylib" 2>/dev/null

    echo "-> $out_dir/libinnowrap.dylib"
}

build

#############################################################################
# Cleanup (keep the per-arch .so folders)
#############################################################################
rm -rf "$TEMP_FOLDER"

echo ""
echo "Done. Output in $TARGET_PREBUILT_FOLDER:"
find "$TARGET_PREBUILT_FOLDER" -name '*.dylib'
