#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${REPO_ROOT}"

usage() {
  cat <<'EOF'
Usage:
  tools/format_cmake_files.sh [--check]

Description:
  Format tracked project-owned CMake files using cmake-format.
  Requires cmakelang[YAML] version 0.6.13 by default.

Options:
  --check   Run cmake-format in check mode without rewriting files.
  -h, --help
EOF
}

MODE="format"
REQUIRED_VERSION="${CMAKE_FORMAT_REQUIRED_VERSION:-0.6.13}"
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

CMAKE_FORMAT_BIN="${CMAKE_FORMAT_BIN:-}"
if [[ -z "${CMAKE_FORMAT_BIN}" ]]; then
  if [[ -x "${REPO_ROOT}/.venv-cmake-format/bin/cmake-format" ]]; then
    CMAKE_FORMAT_BIN="${REPO_ROOT}/.venv-cmake-format/bin/cmake-format"
  elif [[ -x "${REPO_ROOT}/../.venv-cmake-format/bin/cmake-format" ]]; then
    CMAKE_FORMAT_BIN="${REPO_ROOT}/../.venv-cmake-format/bin/cmake-format"
  elif command -v cmake-format >/dev/null 2>&1; then
    CMAKE_FORMAT_BIN="$(command -v cmake-format)"
  else
    echo "cmake-format not found. Set CMAKE_FORMAT_BIN or install cmakelang[YAML]." >&2
    exit 1
  fi
fi

CF_VERSION_OUTPUT="$("${CMAKE_FORMAT_BIN}" --version 2>/dev/null || true)"
CF_VERSION="$(printf '%s\n' "${CF_VERSION_OUTPUT}" | grep -Eo '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)"

if [[ -z "${CF_VERSION}" ]]; then
  echo "Failed to parse cmake-format version from: ${CF_VERSION_OUTPUT}" >&2
  exit 1
fi

if [[ "${CF_VERSION}" != "${REQUIRED_VERSION}" ]]; then
  cat >&2 <<EOF
cmake-format version mismatch.
  required: ${REQUIRED_VERSION}
  found:    ${CF_VERSION} (${CMAKE_FORMAT_BIN})

Install the required version into a local venv:
  python3 -m venv .venv-cmake-format
  .venv-cmake-format/bin/python -m pip install --upgrade pip
  .venv-cmake-format/bin/python -m pip install "cmakelang[YAML]==${REQUIRED_VERSION}"
EOF
  exit 1
fi

list_cmake_files() {
  git ls-files -z -- \
    'CMakeLists.txt' \
    ':(glob)**/CMakeLists.txt' \
    ':(glob)**/*.cmake' \
    ':(exclude)lib/**'
}

if [[ "${MODE}" == "check" ]]; then
  list_cmake_files | xargs -0 -r "${CMAKE_FORMAT_BIN}" \
    --check \
    --config-files "${REPO_ROOT}/.cmake-format.yaml"
  echo "cmake-format check passed for tracked project-owned CMake files."
else
  list_cmake_files | xargs -0 -r "${CMAKE_FORMAT_BIN}" \
    -i \
    --config-files "${REPO_ROOT}/.cmake-format.yaml"
  echo "Formatted tracked project-owned CMake files."
fi
