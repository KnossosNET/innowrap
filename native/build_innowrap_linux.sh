#!/bin/sh
# Builds libinnowrap.so for Linux: x64 (native), arm64 and riscv64 (cross).
#
# Toolchains (all baked into the Docker image, see Dockerfile.innowrap-linux):
#   x64     : host gcc/g++ (native, no cross toolchain)
#   arm64   : aarch64-linux-gnu-gcc / g++   (gcc-aarch64-linux-gnu)
#   riscv64 : riscv64-linux-gnu-gcc / g++   (gcc-riscv64-linux-gnu)
#
# innowrap is a parser-only wrapper around innoextract. It links NO compression
# library, so cross-compiling needs nothing special beyond the C++ toolchain and
# the (header-only) Boost headers (libboost-dev).
#
# The innoextract source is cloned fresh on every run; INNOEXTRACT_REF pins the ref.
#
# Outputs (loose .so per arch):
#   prebuilt_linux_innowrap/x64/libinnowrap.so
#   prebuilt_linux_innowrap/arm64/libinnowrap.so
#   prebuilt_linux_innowrap/riscv64/libinnowrap.so

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# The CMake project (CMakeLists.txt + wrapper/) lives next to this script.
INNOWRAP_SRC="$SCRIPT_DIR"
INNO_FORK="${INNO_FORK:-https://github.com/dscharrer/innoextract.git}"
INNO_REF="${INNO_REF:-6e9e34ed0876014fdb46e684103ef8c3605e382e}"
BOOST_INCLUDEDIR="${BOOST_INCLUDEDIR:-/usr/include}"
TARGET_PREBUILT_FOLDER="$SCRIPT_DIR/prebuilt_linux_innowrap"
# Build OFF the (possibly bind-mounted) script dir. Windows Docker bind mounts
# report file mtimes that make ninja think build.ninja is perpetually dirty
# ("manifest still dirty ... perhaps system time is not set"). A container-local
# temp dir has sane timestamps; only the final .so is copied to the output folder.
TEMP_FOLDER="${INNOWRAP_BUILD_TMP:-/tmp/innowrap_build_linux}"

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
build_one() {
    label="$1"; sysproc="$2"; cc="$3"; cxx="$4"
    build_dir="$TEMP_FOLDER/build-$label"

    echo ""
    echo "=== Building libinnowrap for Linux/$label ==="

    sysname_arg=""; sysproc_arg=""; cc_arg=""; cxx_arg=""
    [ -n "$sysproc" ] && sysname_arg="-DCMAKE_SYSTEM_NAME=Linux"
    [ -n "$sysproc" ] && sysproc_arg="-DCMAKE_SYSTEM_PROCESSOR=$sysproc"
    [ -n "$cc" ]      && cc_arg="-DCMAKE_C_COMPILER=$cc"
    [ -n "$cxx" ]     && cxx_arg="-DCMAKE_CXX_COMPILER=$cxx"

    # shellcheck disable=SC2086
    cmake -S "$PROJ_LOCAL" -B "$build_dir" -G Ninja \
        $sysname_arg $sysproc_arg $cc_arg $cxx_arg \
        -DCMAKE_BUILD_TYPE=Release \
        -DINNOEXTRACT_SRC="$INNO_DIR/src" \
        -DBOOST_INCLUDEDIR="$BOOST_INCLUDEDIR"
    cmake --build "$build_dir" -j"$(nproc)"

    out_dir="$TARGET_PREBUILT_FOLDER/$label"
    mkdir -p "$out_dir"
    cp "$build_dir"/libinnowrap.so "$out_dir"/ 2>/dev/null

    if [ ! -f "$out_dir/libinnowrap.so" ]; then
        echo "Error: libinnowrap.so was not produced for $label" >&2
        exit 1
    fi
    echo "-> $out_dir/libinnowrap.so"
}

build_one "x64"     ""        ""                      ""
build_one "arm64"   "aarch64" "aarch64-linux-gnu-gcc" "aarch64-linux-gnu-g++"
build_one "riscv64" "riscv64" "riscv64-linux-gnu-gcc" "riscv64-linux-gnu-g++"

#############################################################################
# Cleanup (keep the per-arch .so folders)
#############################################################################
rm -rf "$TEMP_FOLDER"

echo ""
echo "Done. Output in $TARGET_PREBUILT_FOLDER:"
find "$TARGET_PREBUILT_FOLDER" -name '*.so'
