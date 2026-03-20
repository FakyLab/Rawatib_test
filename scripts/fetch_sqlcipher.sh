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

mkdir -p "$OUTDIR"
mkdir -p "$TMPDIR_BASE"

echo "Cloning SQLCipher $SQLCIPHER_TAG ..."
git clone --depth=1 --branch "$SQLCIPHER_TAG" \
    https://github.com/sqlcipher/sqlcipher.git \
    "$TMPDIR_BASE/sqlcipher"

cd "$TMPDIR_BASE/sqlcipher"

echo "Configuring ..."
./configure \
    --enable-tempstore=yes \
    CFLAGS="-DSQLITE_HAS_CODEC -DSQLCIPHER_CRYPTO_OPENSSL" \
    LDFLAGS="-lcrypto" \
    --disable-shared \
    --enable-static \
    > /dev/null 2>&1

echo "Generating amalgamation ..."
make sqlite3.c > /dev/null 2>&1

echo "Copying files ..."
cp sqlite3.c  "$OUTDIR/sqlite3.c"
cp sqlite3.h  "$OUTDIR/sqlite3.h"

cd /
rm -rf "$TMPDIR_BASE"

echo ""
echo "=== Done ==="
echo "Files written to $OUTDIR:"
ls -lh "$OUTDIR"
