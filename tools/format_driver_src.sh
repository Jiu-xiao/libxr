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
  Requires clang-format version 21.1.8 by default.

Options:
  --check   Run clang-format in dry-run mode with --Werror.
  -h, --help
EOF
}

MODE="format"
REQUIRED_VERSION="${CLANG_FORMAT_REQUIRED_VERSION:-21.1.8}"
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

CF_VERSION_OUTPUT="$("${CLANG_FORMAT_BIN}" --version 2>/dev/null || true)"
CF_VERSION="$(printf '%s\n' "${CF_VERSION_OUTPUT}" | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"

if [[ -z "${CF_VERSION}" ]]; then
  echo "Failed to parse clang-format version from: ${CF_VERSION_OUTPUT}" >&2
  exit 1
fi

if [[ "${CF_VERSION}" != "${REQUIRED_VERSION}" ]]; then
  cat >&2 <<EOF
clang-format version mismatch.
  required: ${REQUIRED_VERSION}
  found:    ${CF_VERSION} (${CLANG_FORMAT_BIN})

Install the required version into a local venv:
  python3 -m venv .venv-clang-format
  .venv-clang-format/bin/python -m pip install --upgrade pip
  .venv-clang-format/bin/python -m pip install "clang-format==${REQUIRED_VERSION}"
EOF
  exit 1
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
