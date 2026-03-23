#!/usr/bin/env bash
# scripts/fetch_sqlcipher.sh
# Clones SQLCipher, generates the amalgamation, places files in third_party/sqlcipher/
# Run from the project root.

set -e

SQLCIPHER_TAG="v4.13.0"  # update when upgrading SQLCipher
OUTDIR="$(dirname "$0")/../third_party/sqlcipher"
TMPDIR_BASE="/tmp/sqlcipher_build_$$"

echo "=== Rawatib: fetching SQLCipher amalgamation ==="
echo "Tag:    $SQLCIPHER_TAG"
echo "Output: $OUTDIR"
echo ""

# Check prerequisites
if ! command -v tclsh &>/dev/null; then
    echo "ERROR: tclsh not found."
    echo "  Ubuntu/Debian: sudo apt install tcl"
    echo "  macOS:         brew install tcl-tk"
    echo "  Windows/MSYS2: pacman -S tcl"
    exit 1
fi

if ! command -v git &>/dev/null; then
    echo "ERROR: git not found."
    exit 1
fi

if ! command -v make &>/dev/null; then
    echo "ERROR: make not found."
    exit 1
fi

mkdir -p "$OUTDIR"
mkdir -p "$TMPDIR_BASE"

echo "Cloning SQLCipher $SQLCIPHER_TAG ..."
git clone --depth=1 --branch "$SQLCIPHER_TAG" \
    https://github.com/sqlcipher/sqlcipher.git \
    "$TMPDIR_BASE/sqlcipher"

cd "$TMPDIR_BASE/sqlcipher"

if [[ ! -x ./configure ]]; then
    echo "No configure script found. Bootstrapping with autoreconf..."
    if ! command -v autoreconf &>/dev/null; then
        echo "ERROR: autoreconf not found."
        echo "  Ubuntu/Debian: sudo apt install autoconf automake libtool"
        echo "  macOS:         brew install autoconf automake libtool"
        echo "  Windows/MSYS2: pacman -S autoconf automake libtool"
        exit 1
    fi
    autoreconf -fi
fi

echo "Configuring ..."
if ! ./configure \
    --enable-tempstore=yes \
    CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL" \
    LDFLAGS="-lcrypto" \
    --disable-shared \
    --enable-static \
; then
    echo "ERROR: SQLCipher configure failed." >&2
    exit 1
fi

echo "Generating amalgamation ..."
if ! make sqlite3.c; then
    echo "ERROR: SQLCipher amalgamation build failed." >&2
    exit 1
fi

echo "Copying files ..."
cp sqlite3.c  "$OUTDIR/sqlite3.c"
cp sqlite3.h  "$OUTDIR/sqlite3.h"

cd /
rm -rf "$TMPDIR_BASE"

echo ""
echo "=== Done ==="
echo "Files written to $OUTDIR:"
ls -lh "$OUTDIR"
