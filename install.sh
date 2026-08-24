#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

missing=()
for cmd in cmake ninja pkg-config ifuse idevice_id ideviceinfo fusermount3 tar; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
done

if ((${#missing[@]})); then
    echo "Missing commands: ${missing[*]}"
    echo
    echo "On CachyOS/Arch, install the normal build/runtime dependencies with:"
    echo "  sudo pacman -S --needed cmake ninja pkgconf qt6-base taglib glib2 ifuse libimobiledevice"
    echo
    echo "Do NOT replace your locally rebuilt working libgpod package."
    exit 1
fi

if ! pkg-config --exists libgpod-1.0; then
    echo "libgpod-1.0 development metadata was not found."
    echo "Keep the working locally rebuilt libgpod you already installed."
    exit 1
fi

if ! pkg-config --exists taglib; then
    echo "TagLib development metadata was not found. Install it with:"
    echo "  sudo pacman -S --needed taglib"
    exit 1
fi

rm -rf build

cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$HOME/.local"

cmake --build build
cmake --install build

command -v kbuildsycoca6 >/dev/null 2>&1 && kbuildsycoca6 >/dev/null 2>&1 || true

echo
echo "Installed Dad's iPod Transferator 9000 v1.3."
echo "The Qt GUI is isolated from libgpod in a helper process."
echo "Launch it from the KDE application menu or run:"
echo "  $HOME/.local/bin/dads-ipod-transferator-9000"
