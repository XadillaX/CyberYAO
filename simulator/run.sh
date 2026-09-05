#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${SCRIPT_DIR}/build/yaogui_simulator"

if [[ ! -x "${BINARY}" ]]; then
    "${SCRIPT_DIR}/build.sh"
fi

exec "${BINARY}" "$@"
