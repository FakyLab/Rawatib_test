#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 4 ]]; then
    echo "usage: $0 <version> <owner> <repo> <source_sha256>" >&2
    exit 1
fi

version="$1"
owner="$2"
repo="$3"
source_sha256="$4"

out_dir="${AUR_OUT_DIR:-dist/aur}"
mkdir -p "$out_dir"

for template in packaging/aur/PKGBUILD.in packaging/aur/SRCINFO.in; do
    name="$(basename "${template%.in}")"
    sed \
        -e "s/@VERSION@/${version}/g" \
        -e "s/@OWNER@/${owner}/g" \
        -e "s/@REPO@/${repo}/g" \
        -e "s/@SOURCE_SHA256@/${source_sha256}/g" \
        "$template" > "$out_dir/$name"
done
