@echo off
setlocal EnableDelayedExpansion
title Rawatib - Build Script

echo.
echo ========================================
echo   Rawatib - Build Script
echo   Platform: Windows (MinGW)
echo ========================================
echo.

:: -- Locate project root (where this .bat lives) --------------------------
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SCRIPT_DIR%\build"

:: =========================================================================
:: STEP 1 - Find Qt installation
:: =========================================================================

echo [1/5] Searching for Qt 6 installation...
echo.

:: Build a list of all found Qt mingw_64 directories across C, D, E
set QT_COUNT=0

for %%D in (C D E) do (
    if exist "%%D:\Qt\" (
        for /d %%V in ("%%D:\Qt\6.*") do (
            if exist "%%V\mingw_64\lib\cmake\Qt6\Qt6Config.cmake" (
                set /a QT_COUNT+=1
                set "QT_FOUND_!QT_COUNT!=%%V\mingw_64"
                set "QT_LABEL_!QT_COUNT!=%%V\mingw_64"
            )
        )
    )
)

:: Also check if QT_DIR is already set in environment
if defined QT_DIR (
    if exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
        set /a QT_COUNT+=1
        set "QT_FOUND_!QT_COUNT!=%QT_DIR%"
        set "QT_LABEL_!QT_COUNT!=%QT_DIR%  [from QT_DIR env var]"
    )
)

if %QT_COUNT% == 0 (
    echo   ERROR: Qt 6 not found on C:\Qt, D:\Qt, or E:\Qt
    echo.
    echo   Options:
    echo     1. Install Qt 6 from https://www.qt.io/download-open-source
    echo        Choose: Qt 6.x.x ^> MinGW 13.1.0 64-bit
    echo.
    echo     2. Set QT_DIR before running this script:
    echo        set QT_DIR=X:\Qt\6.x.x\mingw_64
    echo        build.bat
    echo.
    echo     3. Open "Qt 6.10.2 (MinGW 13.1.0 64-bit)" from the Start menu
    echo        and run this script from that terminal - Qt will be in PATH.
    echo.
    goto :error
)

if %QT_COUNT% == 1 (
    :: Only one found - use it directly
    set "QT_DIR=%QT_FOUND_1%"
    echo   Found: %QT_LABEL_1%
    echo.
) else (
    :: Multiple found - ask user to choose
    echo   Found %QT_COUNT% Qt installations:
    echo.
    for /l %%I in (1,1,%QT_COUNT%) do (
        echo     [%%I] !QT_LABEL_%%I!
    )
    echo.
    :ask_qt
    set /p QT_CHOICE="  Choose Qt installation [1-%QT_COUNT%]: "
    if "%QT_CHOICE%"=="" goto :ask_qt
    :: Validate input is a number in range
    set "QT_VALID=0"
    for /l %%I in (1,1,%QT_COUNT%) do (
        if "%QT_CHOICE%"=="%%I" set "QT_VALID=1"
    )
    if "!QT_VALID!"=="0" (
        echo   Invalid choice. Please enter a number between 1 and %QT_COUNT%.
        goto :ask_qt
    )
    set "QT_DIR=!QT_FOUND_%QT_CHOICE%!"
    echo.
    echo   Using: !QT_LABEL_%QT_CHOICE%!
    echo.
)

:: =========================================================================
:: STEP 2 - Find MinGW toolchain and add to PATH
:: =========================================================================

echo [2/5] Searching for MinGW toolchain...
echo.

set "MINGW_BIN="

:: Strategy 1: MinGW bundled with Qt installer (Qt\Tools\mingw1310_64)
for %%D in (C D E) do (
    if not defined MINGW_BIN (
        for /d %%M in ("%%D:\Qt\Tools\mingw*_64") do (
            if exist "%%M\bin\gcc.exe" (
                set "MINGW_BIN=%%M\bin"
            )
        )
    )
)

:: Strategy 2: MSYS2 MinGW64 across drives
for %%D in (C D E) do (
    if not defined MINGW_BIN (
        if exist "%%D:\msys64\mingw64\bin\gcc.exe" (
            set "MINGW_BIN=%%D:\msys64\mingw64\bin"
        )
    )
)

:: Strategy 3: already in PATH (e.g. running from Qt MinGW terminal)
if not defined MINGW_BIN (
    where gcc >nul 2>&1
    if !errorlevel! == 0 (
        set "MINGW_BIN=already_in_path"
    )
)

if not defined MINGW_BIN (
    echo   ERROR: MinGW GCC not found.
    echo.
    echo   Options:
    echo     1. Install Qt with the "MinGW 13.1.0 64-bit" component - it
    echo        includes the compiler at Qt\Tools\mingw1310_64\
    echo.
    echo     2. Install MSYS2 from https://www.msys2.org and run:
    echo        pacman -S mingw-w64-ucrt-x86_64-gcc
    echo.
    echo     3. Open "Qt 6.10.2 (MinGW 13.1.0 64-bit)" from the Start menu
    echo        and run this script from that terminal.
    echo.
    goto :error
)

if not "%MINGW_BIN%"=="already_in_path" (
    echo   Found MinGW: %MINGW_BIN%
    set "PATH=%MINGW_BIN%;%PATH%"
) else (
    echo   MinGW already in PATH.
)
echo.

:: =========================================================================
:: STEP 3 - Find OpenSSL
:: =========================================================================

echo [3/5] Searching for OpenSSL...
echo.

set "OPENSSL_ROOT="

:: Strategy 1: Qt installer's optional OpenSSL component (follows Qt drive)
for %%D in (C D E) do (
    if not defined OPENSSL_ROOT (
        if exist "%%D:\Qt\Tools\OpenSSL\Win_x64\include\openssl\ssl.h" (
            set "OPENSSL_ROOT=%%D:\Qt\Tools\OpenSSL\Win_x64"
        )
    )
)

:: Strategy 2: MSYS2 MinGW64 across drives
for %%D in (C D E) do (
    if not defined OPENSSL_ROOT (
        if exist "%%D:\msys64\mingw64\include\openssl\ssl.h" (
            set "OPENSSL_ROOT=%%D:\msys64\mingw64"
        )
    )
)

:: Strategy 3: Standalone Win64 OpenSSL installer (always installs to C:\Program Files)
if not defined OPENSSL_ROOT (
    if exist "C:\Program Files\OpenSSL-Win64\include\openssl\ssl.h" (
        set "OPENSSL_ROOT=C:\Program Files\OpenSSL-Win64"
    )
)

:: Strategy 4: Standalone OpenSSL in Program Files (x86) - less common
if not defined OPENSSL_ROOT (
    if exist "C:\Program Files (x86)\OpenSSL-Win64\include\openssl\ssl.h" (
        set "OPENSSL_ROOT=C:\Program Files (x86)\OpenSSL-Win64"
    )
)

if not defined OPENSSL_ROOT (
    echo   WARNING: OpenSSL not found automatically.
    echo.
    echo   SQLCipher requires OpenSSL. Install one of these:
    echo.
    echo     Option A ^(recommended^): Qt installer OpenSSL component
    echo       Open Qt Maintenance Tool ^> Add components
    echo       Check: Developer and Designer Tools ^> OpenSSL Toolkit
    echo.
    echo     Option B: Win64 OpenSSL Light installer
    echo       https://slproweb.com/products/Win32OpenSSL.html
    echo       Download "Win64 OpenSSL v3.x Light" and install to default path
    echo.
    echo     Option C: MSYS2
    echo       pacman -S mingw-w64-ucrt-x86_64-openssl
    echo.
    set /p OPENSSL_ROOT="  Or enter OpenSSL root path manually (or press Enter to try anyway): "
    echo.
)

if defined OPENSSL_ROOT (
    echo   Found OpenSSL: %OPENSSL_ROOT%
) else (
    echo   Proceeding without explicit OpenSSL path - CMake will try to find it.
)
echo.

:: =========================================================================
:: STEP 4 - Find Ninja (optional, faster than MinGW Makefiles)
:: =========================================================================

set "GENERATOR=MinGW Makefiles"
set "NINJA_BIN="

:: Check Qt Tools\Ninja
for %%D in (C D E) do (
    if not defined NINJA_BIN (
        if exist "%%D:\Qt\Tools\Ninja\ninja.exe" (
            set "NINJA_BIN=%%D:\Qt\Tools\Ninja"
        )
    )
)

:: Check MSYS2
for %%D in (C D E) do (
    if not defined NINJA_BIN (
        if exist "%%D:\msys64\mingw64\bin\ninja.exe" (
            set "NINJA_BIN=%%D:\msys64\mingw64\bin"
        )
    )
)

:: Check already in PATH
if not defined NINJA_BIN (
    where ninja >nul 2>&1
    if !errorlevel! == 0 set "NINJA_BIN=already_in_path"
)

if defined NINJA_BIN (
    if not "%NINJA_BIN%"=="already_in_path" (
        set "PATH=%NINJA_BIN%;%PATH%"
    )
    set "GENERATOR=Ninja"
    echo   Generator: Ninja ^(found^)
) else (
    echo   Generator: MinGW Makefiles ^(Ninja not found^)
)
echo.

:: =========================================================================
:: STEP 5 - CMake configure + build
:: =========================================================================

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

:: Skip configure if CMakeCache.txt already exists - this is a re-run
:: after a previous (possibly partial) build. CMake will still re-check
:: for changed files automatically during the build step.
:: To force a full reconfigure: delete the build\ folder and run again.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [4/5] Build folder found - skipping configure ^(already configured^).
    echo        To force reconfigure: delete the build\ folder and run again.
    echo.
    goto :build
)

echo [4/5] Configuring with CMake...
echo.

:: Build CMake arguments
set "CMAKE_ARGS=%SCRIPT_DIR%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_BUILD_TYPE=Release"
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH=%QT_DIR%"
set "CMAKE_ARGS=%CMAKE_ARGS% -G "%GENERATOR%""

if defined OPENSSL_ROOT (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DOPENSSL_ROOT_DIR=%OPENSSL_ROOT%"
)

cmake %CMAKE_ARGS%
if errorlevel 1 (
    echo.
    echo   ERROR: CMake configuration failed.
    echo   Check the output above for details.
    goto :error
)

:build

echo.
echo [5/5] Building...
echo.

cmake --build . --config Release -j%NUMBER_OF_PROCESSORS%
if errorlevel 1 (
    echo.
    echo   ERROR: Build failed.
    echo   Check the output above for details.
    goto :error
)

:: =========================================================================
:: Done
:: =========================================================================

echo.
echo ========================================
echo   Build Complete!
echo ========================================
echo.
echo   Executable : %BUILD_DIR%\Rawatib.exe
echo.
echo   Run with   : %BUILD_DIR%\Rawatib.exe
echo.
echo   Note: windeployqt runs automatically during the build if found.
echo         If Qt DLLs are missing next to the .exe, run manually:
echo         %QT_DIR%\..\..\..\bin\windeployqt.exe %BUILD_DIR%\Rawatib.exe
echo.
goto :end

:error
echo.
echo ========================================
echo   Build FAILED - see errors above.
echo ========================================
echo.

:end
pause
endlocal
