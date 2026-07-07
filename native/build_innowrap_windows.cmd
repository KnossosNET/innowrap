@echo off
setlocal

cd /d "%~dp0"

REM ===========================================================================
REM Builds innowrap.dll for Windows x64, x86 and ARM64.
REM Clones innoextract into innoextract\ and builds the innowrap project (the
REM CMakeLists.txt + wrapper\ next to this script).
REM Requires: Visual Studio 2022 (Desktop C++ + the ARM64 build tools), CMake,
REM and the Boost headers (set BOOST_INCLUDEDIR below).
REM Output DLLs are left in build-win-<arch>\Release\innowrap.dll
REM ===========================================================================

set "FORK=https://github.com/dscharrer/innoextract.git"
set "INNO_REF=6e9e34ed0876014fdb46e684103ef8c3605e382e"

REM --- Boost headers (header-only). Override here or set BOOST_INCLUDEDIR in env.
if "%BOOST_INCLUDEDIR%"=="" set "BOOST_INCLUDEDIR=C:\boost_1_91_0"
if not exist "%BOOST_INCLUDEDIR%\boost\version.hpp" (
    echo ERROR: Boost headers not found at "%BOOST_INCLUDEDIR%".
    echo        Download Boost ^(e.g. boost_1_91_0^), unzip it, and set
    echo        BOOST_INCLUDEDIR to that folder ^(no need to build Boost^).
    goto :error
)

echo ========================================
echo Cleanup
echo ========================================
if exist "innoextract"    rmdir /s /q "innoextract"
if exist "build-win-x64"   rmdir /s /q "build-win-x64"
if exist "build-win-x86"   rmdir /s /q "build-win-x86"
if exist "build-win-arm64" rmdir /s /q "build-win-arm64"

echo ========================================
echo Cloning innoextract...
echo ========================================
git clone "%FORK%" "innoextract"
if errorlevel 1 goto :error
git -C "innoextract" checkout "%INNO_REF%"
if errorlevel 1 goto :error

echo ========================================
echo Build x64...
echo ========================================
cmake -S "." -B "build-win-x64" -G "Visual Studio 17 2022" -A x64 -DINNOEXTRACT_SRC="%CD%/innoextract/src" -DBOOST_INCLUDEDIR="%BOOST_INCLUDEDIR%"
if errorlevel 1 goto :error
cmake --build "build-win-x64" --config Release
if errorlevel 1 goto :error

echo ========================================
echo Build x86...
echo ========================================
cmake -S "." -B "build-win-x86" -G "Visual Studio 17 2022" -A Win32 -DINNOEXTRACT_SRC="%CD%/innoextract/src" -DBOOST_INCLUDEDIR="%BOOST_INCLUDEDIR%"
if errorlevel 1 goto :error
cmake --build "build-win-x86" --config Release
if errorlevel 1 goto :error

echo ========================================
echo Build ARM64...
echo ========================================
cmake -S "." -B "build-win-arm64" -G "Visual Studio 17 2022" -A ARM64 -DINNOEXTRACT_SRC="%CD%/innoextract/src" -DBOOST_INCLUDEDIR="%BOOST_INCLUDEDIR%"
if errorlevel 1 goto :error
cmake --build "build-win-arm64" --config Release
if errorlevel 1 goto :error

echo.
echo ========================================
echo BUILD COMPLETED
echo   build-win-x64\Release\innowrap.dll
echo   build-win-x86\Release\innowrap.dll
echo   build-win-arm64\Release\innowrap.dll
echo ========================================
if exist "innoextract" rmdir /s /q "innoextract"
pause
exit /b 0

:error
echo.
echo ========================================
echo ERROR
echo ========================================
pause
exit /b 1
