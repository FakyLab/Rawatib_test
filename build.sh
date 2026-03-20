#!/bin/bash
# Build script for Rawatib
# Supports: Linux, macOS, Windows (MSYS2/MinGW)
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# ── Detect OS ─────────────────────────────────────────────────────────────
case "$(uname -s)" in
    Linux*)   OS="linux"   ;;
    Darwin*)  OS="macos"   ;;
    MINGW*|MSYS*|CYGWIN*)  OS="windows" ;;
    *)        OS="unknown" ;;
esac

echo "========================================"
echo "  Rawatib - Build Script"
echo "  Platform: $OS"
echo "========================================"

# ── Upfront checks ────────────────────────────────────────────────────────

if ! command -v cmake &>/dev/null; then
    echo ""
    echo "ERROR: cmake not found."
    echo ""
    echo "Install cmake:"
    case "$OS" in
        linux)   echo "  Ubuntu/Debian : sudo apt install cmake" ;;
        macos)   echo "  macOS         : brew install cmake" ;;
        windows) echo "  Windows/MSYS2 : pacman -S mingw-w64-ucrt-x86_64-cmake" ;;
    esac
    exit 1
fi

# Check Qt — look for qmake or Qt6Config.cmake
QT_FOUND=0
if command -v qmake6 &>/dev/null || command -v qmake &>/dev/null; then
    QT_FOUND=1
elif [ -n "${QT_DIR:-}" ] && [ -d "$QT_DIR" ]; then
    QT_FOUND=1
elif [ -n "${CMAKE_PREFIX_PATH:-}" ]; then
    QT_FOUND=1
fi

if [ "$QT_FOUND" -eq 0 ]; then
    echo ""
    echo "WARNING: Qt6 not detected automatically."
    echo "  If Qt is installed, set QT_DIR before running this script:"
    echo ""
    case "$OS" in
        linux)
            echo "  export QT_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt6"
            echo "  or install: sudo apt install qt6-base-dev qt6-tools-dev"
            ;;
        macos)
            echo "  export QT_DIR=\$(brew --prefix qt6)/lib/cmake/Qt6"
            echo "  or install: brew install qt6"
            ;;
        windows)
            echo "  export QT_DIR=C:/Qt/6.x.x/mingw_64/lib/cmake/Qt6"
            echo "  or install Qt from https://www.qt.io/download"
            ;;
    esac
    echo ""
    echo "  Continuing — CMake will error if Qt6 cannot be found."
    echo ""
fi

# Check OpenSSL
OPENSSL_FOUND=0
if command -v openssl &>/dev/null; then
    OPENSSL_FOUND=1
elif pkg-config --exists openssl 2>/dev/null; then
    OPENSSL_FOUND=1
fi

if [ "$OPENSSL_FOUND" -eq 0 ]; then
    echo ""
    echo "WARNING: OpenSSL not detected. SQLCipher requires OpenSSL headers."
    echo "  Install OpenSSL development libraries:"
    case "$OS" in
        linux)   echo "  sudo apt install libssl-dev          # Ubuntu/Debian"
                 echo "  sudo dnf install openssl-devel       # Fedora/RHEL" ;;
        macos)   echo "  brew install openssl"
                 echo "  export OPENSSL_ROOT_DIR=\$(brew --prefix openssl)" ;;
        windows) echo "  pacman -S mingw-w64-ucrt-x86_64-openssl" ;;
    esac
    echo ""
    echo "  Continuing — CMake will error if OpenSSL cannot be found."
    echo ""
fi

# ── Configure ─────────────────────────────────────────────────────────────

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo ""
echo "[1/3] Configuring with CMake..."

# Pick generator: prefer Ninja, fall back to platform default
if command -v ninja &>/dev/null; then
    GENERATOR="Ninja"
else
    case "$OS" in
        windows) GENERATOR="MinGW Makefiles" ;;
        *)       GENERATOR="Unix Makefiles" ;;
    esac
fi

# macOS: if OPENSSL_ROOT_DIR not set but Homebrew openssl exists, set it
if [ "$OS" = "macos" ] && [ -z "${OPENSSL_ROOT_DIR:-}" ]; then
    if command -v brew &>/dev/null; then
        BREW_OPENSSL="$(brew --prefix openssl 2>/dev/null || true)"
        if [ -n "$BREW_OPENSSL" ] && [ -d "$BREW_OPENSSL" ]; then
            export OPENSSL_ROOT_DIR="$BREW_OPENSSL"
            echo "  Auto-detected Homebrew OpenSSL: $OPENSSL_ROOT_DIR"
        fi
    fi
fi

cmake "$SCRIPT_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${QT_DIR:-}" \
    -G "$GENERATOR"

# ── Build ─────────────────────────────────────────────────────────────────

echo ""
echo "[2/3] Building..."

# Detect CPU count cross-platform
if command -v nproc &>/dev/null; then
    JOBS="$(nproc)"
elif command -v sysctl &>/dev/null; then
    JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
else
    JOBS=4
fi

cmake --build . --config Release -j"$JOBS"

# ── Done ──────────────────────────────────────────────────────────────────

echo ""
echo "[3/3] Done!"
echo ""

case "$OS" in
    windows)
        echo "Executable: $BUILD_DIR/Rawatib.exe"
        echo ""
        echo "Run with:   $BUILD_DIR/Rawatib.exe"
        echo ""
        echo "Note: windeployqt runs automatically during the build if found."
        echo "      If Qt DLLs are missing, run manually:"
        echo "      windeployqt $BUILD_DIR/Rawatib.exe"
        ;;
    macos)
        echo "App bundle: $BUILD_DIR/Rawatib.app"
        echo ""
        echo "Run with:   open $BUILD_DIR/Rawatib.app"
        echo "        or: $BUILD_DIR/Rawatib.app/Contents/MacOS/Rawatib"
        echo ""
        echo "Note: macdeployqt runs automatically during the build if found."
        echo "      If Qt frameworks are missing, run manually:"
        echo "      macdeployqt $BUILD_DIR/Rawatib.app"
        ;;
    linux)
        echo "Executable: $BUILD_DIR/Rawatib"
        echo ""
        echo "Run with:   $BUILD_DIR/Rawatib"
        echo ""
        echo "To install system-wide:"
        echo "  cmake --install $BUILD_DIR --prefix /usr/local"
        echo ""
        echo "Note: Qt libraries must be installed system-wide for the binary"
        echo "      to run on other machines. For standalone distribution,"
        echo "      use linuxdeployqt or package as AppImage."
        ;;
    *)
        echo "Executable: $BUILD_DIR/Rawatib"
        echo ""
        echo "Run with:   $BUILD_DIR/Rawatib"
        ;;
esac
echo ""
