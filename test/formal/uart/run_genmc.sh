#!/usr/bin/env bash
set -euo pipefail

IMAGE_DIGEST="sha256:f9a74c13505b07a0cfcc96d5c46813d975543a081d56449cd529ba2a2f9d4791"
IMAGE_REF="genmc/genmc@${IMAGE_DIGEST}"
GENMC_VERSION="v0.17.0"
GENMC_BINARY="/usr/local/bin/genmc/genmc"
SUCCESS_SIGNATURE="No errors were detected."

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../../.." && pwd)"

command -v docker >/dev/null 2>&1 || {
  echo "docker is required" >&2
  exit 1
}

if ! docker image inspect "${IMAGE_REF}" >/dev/null 2>&1; then
  echo "Pulling pinned GenMC image ${IMAGE_REF}..."
  docker pull "${IMAGE_REF}"
fi

repo_digests="$(docker image inspect --format '{{json .RepoDigests}}' "${IMAGE_REF}")"
if [[ "${repo_digests}" != *"${IMAGE_REF}"* ]]; then
  echo "Local GenMC image does not contain the pinned digest ${IMAGE_REF}" >&2
  exit 1
fi

version_output="$(docker run --rm --pull=never "${IMAGE_REF}" \
  "${GENMC_BINARY}" --version 2>&1)"
if [[ "${version_output}" != *"GenMC ${GENMC_VERSION}"* ]]; then
  echo "Expected GenMC ${GENMC_VERSION} at ${GENMC_BINARY}" >&2
  exit 1
fi
echo "Using GenMC ${GENMC_VERSION} from ${IMAGE_REF}"

run_case() {
  local name="$1"
  local expectation="$2"
  local expected_text="$3"
  shift 3

  local output
  local status
  echo
  echo "== GenMC: ${name} =="
  set +e
  output="$(docker run --rm --pull=never \
    -v "${repo_root}:/work:ro" -w /tmp \
    "${IMAGE_REF}" "${GENMC_BINARY}" "$@" 2>&1)"
  status=$?
  set -e
  printf '%s\n' "${output}"

  if [[ "${expectation}" == "success" ]]; then
    if [[ ${status} -ne 0 ]]; then
      echo "${name} failed with exit code ${status}" >&2
      return 1
    fi
    if ! grep -Fq "${SUCCESS_SIGNATURE}" <<<"${output}"; then
      echo "${name} exited zero without '${SUCCESS_SIGNATURE}'" >&2
      return 1
    fi
    echo "PASS: ${name}"
    return 0
  fi

  if [[ ${status} -eq 0 ]]; then
    echo "${name} unexpectedly passed" >&2
    return 1
  fi
  if ! grep -Fq "${expected_text}" <<<"${output}"; then
    echo "${name} failed without the expected signature '${expected_text}'" >&2
    return 1
  fi
  echo "PASS: ${name} produced the expected violation"
}

COMMON_FLAGS=( -std=c++20 -pthread )
GATE_FLAGS=(
  -std=c++20
  -pthread
  -fno-threadsafe-statics
  -D_BITS_PTHREADTYPES_COMMON_H=1
  -I/work/src/driver/model
  -I/work/src/core
  -I/work/src/core/assert
)

run_case "AtomicFrontendRC11" success "" \
  --rc11 -- "${COMMON_FLAGS[@]}" \
  /work/test/formal/uart/common/atomic_frontend_probe.cpp

run_case "AtomicFrontendBroken" failure "Safety violation" \
  --rc11 -- "${COMMON_FLAGS[@]}" -DBROKEN_MEMORY_ORDER=1 \
  /work/test/formal/uart/common/atomic_frontend_probe.cpp

run_case "SerializedServiceLiveness" success "" \
  --rc11 --check-liveness -- "${COMMON_FLAGS[@]}" \
  -I/work/src/driver/model \
  /work/test/formal/uart/genmc/serialized_service.cpp

run_case "UartHardwareGateSafety" success "" \
  --rc11 -- "${GATE_FLAGS[@]}" \
  /work/test/formal/uart/genmc/uart_hardware_gate.cpp

run_case "UartHardwareGateLiveness" success "" \
  --rc11 --check-liveness -- "${GATE_FLAGS[@]}" \
  /work/test/formal/uart/genmc/uart_hardware_gate.cpp

run_case "UartHardwareGateNestedSafety" success "" \
  --rc11 -- "${GATE_FLAGS[@]}" \
  /work/test/formal/uart/genmc/uart_hardware_gate_nested.cpp

run_case "UartHardwareGateNestedLiveness" success "" \
  --rc11 --check-liveness -- "${GATE_FLAGS[@]}" \
  /work/test/formal/uart/genmc/uart_hardware_gate_nested.cpp

echo
echo "All GenMC expectations passed."
