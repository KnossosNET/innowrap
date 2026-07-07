# innowrap — minimal native innoextract wrapper for Knossos.NET

Replaces the use of the `innoextract` executable (CLI) with a very small
**parser‑only** native library (`innowrap`) plus a C# wrapper that does **all the
decompression with SharpCompress**.

## Core idea

To extract each file, innoextract does this: it locates the *chunk*, reads the
`zlb\x1a` magic, decompresses the chunk (Stored / Zlib / BZip2 / LZMA1 / LZMA2),
skips to the file's offset, reads its size, and applies a per‑file filter. All of
that can be done in C# with SharpCompress **as long as you have the metadata**.

So the split of work is:

* **C++ (`innowrap`)** only *parses* the installer: loader offsets + setup headers
  + file/data entries. It does **no file I/O** (the stream is provided by C# via
  callback) and **links no compression library**. The only compressed thing the
  parser needs is the *header block* (the file list): its decompression is
  delegated back to C# via callback.
* **C# (`InnoArchive`)** owns the `.exe` stream, feeds the parser, and extracts
  each file with SharpCompress using the returned metadata.

Result: the native library weighs ~280 KB and depends **only** on
libc/libstdc++/libm/libgcc. No boost.iostreams, no zlib, no bzip2, no lzma, no
filesystem. All compression lives in C# (SharpCompress).

## Package contents

```
innowrap/
├─ native/
│  ├─ CMakeLists.txt         Compiles the minimal subset of innoextract.
│  ├─ build.sh               Clones innoextract at the validated commit and builds.
│  ├─ patches/
│  │  └─ block.cpp.patch     Diff vs upstream of stream/block.cpp.
│  └─ wrapper/
│     ├─ innowrap.cpp        The C ABI (parser + callbacks).
│     ├─ innowrap.h          C header of the ABI (for reference/other bindings).
│     ├─ innowrap_cb.hpp     Filter that delegates the header inflate to C#.
│     ├─ block.cpp           Patched stream/block.cpp (replaces the upstream one).
│     ├─ extras.cpp          Symbols from the .cpp files we do NOT compile.
│     ├─ configure.hpp       Fixed config (LZMA/decryption/iconv = off).
│     └─ release.cpp         Version strings.
├─ csharp/
   ├─ InnoArchive.cs         P/Invoke + callbacks + extraction with SharpCompress.
   ├─ InnoExeFilters.cs      Port of Inno's exe filters (CALL/JMP).
   └─ Fs2InstallerExample.cs Example that replaces the ViewModel flow.
```

## Multi‑platform build scripts (etc2native style)

Under `native/` there is a build kit in the same style as your etc2native
scripts, which clones innoextract at the pinned commit and builds for every
target:

* **Windows (MSVC):** `build_innowrap_windows.cmd` → x64, x86 (Win32) and ARM64.
  Requires VS 2022 (Desktop C++ + the ARM64 build tools), CMake and the Boost
  headers (set `BOOST_INCLUDEDIR`, default `C:\boost_1_85_0`). Output:
  `build-win-<arch>\Release\innowrap.dll`.
* **Linux (Docker):** image `Dockerfile.innowrap-linux` + `build_innowrap_linux.sh`
  → x64 (native), arm64 and riscv64 (cross). Output:
  `prebuilt_linux_innowrap/<arch>/libinnowrap.so`.
* **Android (Docker):** image `Dockerfile.innowrap-android` (NDK r29 baked in) +
  `build_innowrap_android.sh` → arm64‑v8a, armeabi‑v7a, x86, x86_64. Output:
  `prebuilt_android_innowrap/<abi>/libinnowrap.so`.

With docker compose:

```bash
cd native
docker compose -f compose.innowrap.yaml build
docker compose -f compose.innowrap.yaml run --rm innowrap-linux
docker compose -f compose.innowrap.yaml run --rm innowrap-android
```

## Building the native library (manual, single target)

```bash
git clone https://github.com/dscharrer/innoextract.git
git -C innoextract checkout 6e9e34ed0876014fdb46e684103ef8c3605e382e
cmake -B build -S native -DINNOEXTRACT_SRC=$PWD/innoextract/src -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`CMakeLists.txt` deliberately compiles a subset: `util/*` (without `test.cpp`,
`windows.cpp`, `time.cpp`), `crypto/*` (without `arc4.cpp`/`xchacha20.cpp`),
`loader/*`, `setup/*`, and **only** our patched `block.cpp` from `stream/`
(`chunk.cpp`/`file.cpp`/`slice.cpp`/`lzma.cpp` are not compiled: those are the
ones that pull in the compression libraries).

### Cross‑compile (all 11 targets)

* **Windows (x86 / x64 / arm64)** — MSVC (Visual Studio 2022). See the "Building
  on Windows with MSVC" section below. The ABI uses `int64_t` in the callbacks
  (never C's `long`, which is 32‑bit on Windows), so the C# marshalling is
  identical on all platforms.
* **Linux (x86 / x64 / arm64 / riscv64)** — native or cross gcc/clang
  (`-DCMAKE_TOOLCHAIN_FILE=...`). riscv64: `riscv64-linux-gnu-g++` toolchain.
* **Android (arm32 / arm64 / x86 / x64)** — NDK r25+. Per ABI:

  ```bash
  cmake -B build-android-arm64 -S native \
    -DINNOEXTRACT_SRC=$PWD/innoextract/src \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-21 \
    -DBOOST_INCLUDEDIR=/path/to/boost -DCMAKE_BUILD_TYPE=Release
  cmake --build build-android-arm64
  ```

  Repeat with `armeabi-v7a`, `x86`, `x86_64`.

## Building on Windows with MSVC

Requirements: **Visual Studio 2022** with the *"Desktop development with C++"*
workload (which brings MSVC + CMake + Ninja), and the **Boost headers**.

1. Boost headers (no need to build anything from Boost). The simplest way is to
   download the Boost `.zip` (e.g. `boost_1_85_0`) and unzip it; point
   `-DBOOST_INCLUDEDIR=` at that root folder. (vcpkg also works:
   `vcpkg install boost-iostreams boost-filesystem boost-config` and then pass
   the vcpkg toolchain.)

2. Clone innoextract at the validated commit:

   ```bat
   git clone https://github.com/dscharrer/innoextract.git
   git -C innoextract checkout 6e9e34ed0876014fdb46e684103ef8c3605e382e
   ```

3. Configure and build (from a *Developer PowerShell/CMD for VS 2022*):

   ```bat
   :: x64
   cmake -B build-x64 -S native -G "Visual Studio 17 2022" -A x64 ^
         -DINNOEXTRACT_SRC=%CD%\innoextract\src ^
         -DBOOST_INCLUDEDIR=C:\boost_1_85_0
   cmake --build build-x64 --config Release
   :: -> build-x64\Release\innowrap.dll

   :: x86 (32-bit):  -A Win32   -> build-x86\Release\innowrap.dll
   :: arm64:         -A ARM64   -> build-arm64\Release\innowrap.dll
   ```

   `CMakeLists.txt` already applies what's needed for MSVC: `/O2 /EHsc`,
   `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, output `innowrap.dll` without the `lib`
   prefix, and `configure.hpp` selects `_byteswap_*` / `_mkgmtime` instead of
   GCC's `__builtin_*`. No compression library is linked.

4. Place the DLL. For `DllImport("innowrap")` to resolve it, add it to the
   Avalonia/.NET project as a native asset per RID, so that it ends up in
   `runtimes\win-x64\native\innowrap.dll` (and `win-x86`, `win-arm64`). In the
   `.csproj`:

   ```xml
   <ItemGroup>
     <None Include="native\win-x64\innowrap.dll"
           Link="runtimes\win-x64\native\innowrap.dll">
       <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
     </None>
   </ItemGroup>
   ```


## Using it from C#

```csharp
using KnossosNET.Inno;

// Validate (replaces: innoextract <exe> -l -g)
// Use the PATH constructor so it can find the sibling .bin files.
using (var a = new InnoArchive(gogExe))   // gogExe = path to the .exe
{
    foreach (var f in a.Files) Console.WriteLine($"{f.Name}  {f.Size} bytes ({f.PartCount} parts)");
    var vp = a.FindFile("root_fs2.vp");
}

// Extract a file to streams (replaces: innoextract <exe> -L -g -d <dir>)
using (var a = new InnoArchive(gogExe))
using (var outFs = File.Create(Path.Combine(fs2Dir, "root_fs2.vp")))
{
    a.ExtractTo(a.FindFile("root_fs2.vp")!, outFs);  // concatenates + inflates all parts
}
```