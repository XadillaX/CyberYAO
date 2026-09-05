#!/usr/bin/env bash
set -euo pipefail

mode="${1:---check}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
clang_format="${CLANG_FORMAT_BIN:-${repo_root}/node_modules/.bin/clang-format}"
cpplint="${CPPLINT_BIN:-${repo_root}/node_modules/.bin/cpplint}"

if [[ "${mode}" != "--check" && "${mode}" != "--fix" ]]; then
    echo "Usage: $0 [--check|--fix]" >&2
    exit 2
fi

for tool in "${clang_format}" "${cpplint}"; do
    if [[ ! -x "${tool}" ]]; then
        echo "ERROR: ${tool} is unavailable; run 'npm ci --ignore-scripts'." >&2
        exit 1
    fi
done

cd "${repo_root}"
node -e '
const expected = {"clang-format": "1.6.0", "cpplint.js": "1.0.0"};
for (const [name, version] of Object.entries(expected)) {
  const actual = require(`${name}/package.json`).version;
  if (actual !== version) {
    throw new Error(`${name}: expected ${version}, found ${actual}`);
  }
}
'

sources=()
while IFS= read -r -d '' file; do
    case "${file}" in
        managed_components/*|generated/*|build/*|*/yaogui_text_data.c)
            continue
            ;;
    esac
    sources+=("${file}")
done < <(git ls-files -z -- '*.c' '*.h')

if [[ "${#sources[@]}" -eq 0 ]]; then
    echo "ERROR: no tracked C sources found." >&2
    exit 1
fi

if [[ "${mode}" == "--fix" ]]; then
    "${clang_format}" -i --style=file "${sources[@]}"
else
    format_failed=0
    formatted="$(mktemp "${TMPDIR:-/tmp}/yaogui-clang-format.XXXXXX")"
    trap 'rm -f -- "${formatted}"' EXIT
    for file in "${sources[@]}"; do
        "${clang_format}" --style=file "${file}" >"${formatted}"
        if ! cmp -s "${file}" "${formatted}"; then
            echo "clang-format: ${file}" >&2
            format_failed=1
        fi
    done
    if [[ "${format_failed}" -ne 0 ]]; then
        echo "ERROR: C formatting differs; run 'npm run format:c'." >&2
        exit 1
    fi
fi

"${cpplint}" "${sources[@]}"
echo "C lint: PASS (${#sources[@]} files)"
