#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${BUILD_DIR:-$project_root/build}"
dist_dir="${DIST_DIR:-$project_root/dist/linux}"
qt_prefix="${QT_DIR:-}"

mkdir -p "$dist_dir"

cmake -S "$project_root" -B "$build_dir" \
  -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  ${qt_prefix:+-DCMAKE_PREFIX_PATH="$qt_prefix"}

cmake --build "$build_dir" --config Release
cpack --config "$build_dir/CPackConfig.cmake" -G DEB -B "$dist_dir"

appdir="$dist_dir/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir"
DESTDIR="$appdir" cmake --install "$build_dir" --prefix /usr

linuxdeploy="${LINUXDEPLOY:-$dist_dir/linuxdeploy-x86_64.AppImage}"
qt_plugin="${LINUXDEPLOY_QT_PLUGIN:-$dist_dir/linuxdeploy-plugin-qt-x86_64.AppImage}"

if [[ ! -x "$linuxdeploy" || ! -x "$qt_plugin" ]]; then
    echo "linuxdeploy and linuxdeploy-plugin-qt must exist and be executable." >&2
    exit 1
fi

export ARCH=x86_64
export VERSION="${VERSION:-$(grep -oP 'project\(Rawatib VERSION \K[0-9.]+(?= )' "$project_root/CMakeLists.txt" | head -n1)}"

"$linuxdeploy" \
  --appdir "$appdir" \
  --desktop-file "$appdir/usr/share/applications/com.fakylab.rawatib.desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/256x256/apps/rawatib.png" \
  --plugin qt \
  --output appimage

generated_appimage=$(find "$dist_dir" -maxdepth 1 -type f -name '*.AppImage' ! -name 'linuxdeploy*.AppImage' | head -n1)
if [[ -z "$generated_appimage" ]]; then
    echo "No generated AppImage found in $dist_dir" >&2
    exit 1
fi

mv "$generated_appimage" "$dist_dir/rawatib-${VERSION}-x86_64.AppImage"
