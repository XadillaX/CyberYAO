#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

if command -v brew >/dev/null 2>&1; then
    SDL_PREFIX="$(brew --prefix sdl2 2>/dev/null ||
                  brew --prefix sdl2-compat 2>/dev/null || true)"
    if [[ -n "${SDL_PREFIX}" ]]; then
        export PKG_CONFIG_PATH="${SDL_PREFIX}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
    fi
fi

if ! command -v pkg-config >/dev/null 2>&1 ||
   ! pkg-config --exists sdl2; then
    echo "未找到系统 SDL2。请先执行: brew install sdl2 pkg-config" >&2
    exit 1
fi

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug
cmake --build "${BUILD_DIR}" --parallel
echo "模拟器已构建: ${BUILD_DIR}/yaogui_simulator"
