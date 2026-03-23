#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$project_root/build}"
dist_dir="${DIST_DIR:-$project_root/dist/macos}"
qt_prefix="${QT_DIR:-}"
openssl_root="${OPENSSL_ROOT_DIR:-}"

mkdir -p "$dist_dir"

cmake -S "$project_root" -B "$build_dir" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  ${qt_prefix:+-DCMAKE_PREFIX_PATH="$qt_prefix"} \
  ${openssl_root:+-DOPENSSL_ROOT_DIR="$openssl_root"}

cmake --build "$build_dir" --config Release

app_path="$build_dir/Rawatib.app"
if [[ ! -d "$app_path" ]]; then
    echo "Expected app bundle not found at $app_path" >&2
    exit 1
fi

macdeployqt "${app_path}" -dmg -always-overwrite

version="${VERSION:-$(grep -oP 'project\(Rawatib VERSION \K[0-9.]+(?= )' "$project_root/CMakeLists.txt" | head -n1)}"
dmg_path="$build_dir/Rawatib.dmg"
mv "$dmg_path" "$dist_dir/rawatib-${version}-macos.dmg"
