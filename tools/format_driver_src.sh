#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

usage() {
  cat <<'EOF'
Usage:
  tools/format_driver_src.sh [--check]

Description:
  Format all C/C++ source files under driver/ and src/ using clang-format.

Options:
  --check   Run clang-format in dry-run mode with --Werror.
  -h, --help
EOF
}

MODE="format"
case "${1:-}" in
  "")
    ;;
  --check)
    MODE="check"
    ;;
  -h|--help)
    usage
    exit 0
    ;;
  *)
    echo "Unknown option: ${1}" >&2
    usage >&2
    exit 2
    ;;
esac

CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-}"
if [[ -z "${CLANG_FORMAT_BIN}" ]]; then
  if [[ -x "${REPO_ROOT}/.venv-clang-format/bin/clang-format" ]]; then
    CLANG_FORMAT_BIN="${REPO_ROOT}/.venv-clang-format/bin/clang-format"
  elif [[ -x "${REPO_ROOT}/../.venv-clang-format/bin/clang-format" ]]; then
    CLANG_FORMAT_BIN="${REPO_ROOT}/../.venv-clang-format/bin/clang-format"
  elif command -v clang-format >/dev/null 2>&1; then
    CLANG_FORMAT_BIN="$(command -v clang-format)"
  else
    echo "clang-format not found. Set CLANG_FORMAT_BIN or install clang-format." >&2
    exit 1
  fi
fi

if [[ "${MODE}" == "check" ]]; then
  rg --files -0 driver src -g '*.{c,cc,cpp,cxx}' | \
    xargs -0 -r "${CLANG_FORMAT_BIN}" --dry-run --Werror --style=file
  echo "clang-format check passed for driver/ and src/."
else
  rg --files -0 driver src -g '*.{c,cc,cpp,cxx}' | \
    xargs -0 -r "${CLANG_FORMAT_BIN}" -i --style=file
  echo "Formatted C/C++ sources under driver/ and src/."
fi
