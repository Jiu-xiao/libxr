#!/usr/bin/env bash
set -euo pipefail

TLA_VERSION="1.7.4"
TLC_VERSION="2.19"
JAR_URL="https://github.com/tlaplus/tlaplus/releases/download/v1.7.4/tla2tools.jar"
JAR_SHA256="936a262061c914694dfd669a543be24573c45d5aa0ff20a8b96b23d01e050e88"
SERVICE_BROKEN_SIGNATURE="Invariant NoLostEvents is violated"
EPHEMERAL_RETRY_SIGNATURE="Invariant TxWorkHasCarrier is violated"
MISSING_SELF_DISPATCH_SIGNATURE="Invariant DeferredWorkHasDispatcher is violated"
BOUND_SUBMIT_CONTEXT_SIGNATURE="Invariant TxWorkHasCarrier is violated"
MOVING_BOUNDARY_SIGNATURE="Action property ConfigBoundaryDoesNotMove is violated"
SUBMIT_AFTER_TAKE_SIGNATURE="Invariant NoSubmitAfterTakeWitness is violated"
OWNER_RETAKE_SIGNATURE="Invariant NoOwnerRetakeWitness is violated"
NO_REMASK_SIGNATURE="Invariant ScanRequiresMask is violated"
LATE_STATUS_SIGNATURE="Invariant StatusMatchesActiveGeneration is violated"
EARLY_QUIESCE_SIGNATURE="Invariant QuiescenceRequiresSettledAbort is violated"
ABORT_ABA_SIGNATURE="Invariant OldAbortCannotEnterNewAdmission is violated"
START_WINDOW_EARLY_MUTATION_SIGNATURE="Invariant ExternalPathsArePublishOnly is violated"
START_WINDOW_DOUBLE_TERMINAL_SIGNATURE="Invariant ErrorAbsorbsComplete is violated"
START_WINDOW_MISSING_FAILURE_CONFIG_SIGNATURE="Invariant FailedHasConfigCarrier is violated"
START_WINDOW_STARTED_PRECOMMIT_WITNESS_SIGNATURE="Invariant NoStartedPreCommitWindowWitness is violated"
START_WINDOW_FAILED_CONFIG_PRECOMMIT_WITNESS_SIGNATURE="Invariant NoFailedConfigPreCommitWindowWitness is violated"
START_WINDOW_FAILED_CALLBACK_PRECONFIG_WITNESS_SIGNATURE="Invariant NoFailedCallbackPreConfigWindowWitness is violated"
PIPELINE_EARLY_PROMOTE_SIGNATURE="Invariant PromotedPendingWasPublished is violated"
PIPELINE_DOUBLE_FINISH_SIGNATURE="Invariant FinishAtMostOnce is violated"
PIPELINE_RETRY_CONSUME_SIGNATURE="Invariant RetryKeepsRecordOwned is violated"
PIPELINE_WRONG_COMPLETION_SIGNATURE="Action property CompletionIdentityMatchesActiveRecord is violated"
PIPELINE_OWNER_PUBLICATION_SIGNATURE="Action property NoWriterPublicationWhileOwnerWitness is violated"
RX_MOVING_BOUNDARY_SIGNATURE="Invariant NoPostSnapshotLoss is violated"
RX_STALE_RELEASE_EVENT_SIGNATURE="Invariant PostSnapshotDataReturnsEvent is violated"
CONFIG_DRAIN_MOVING_BOUNDARY_SIGNATURE="Invariant PostBoundarySurvives is violated"
CONFIG_DRAIN_PENDING_PAYLOAD_SIGNATURE="Invariant MetaDataAligned is violated"
WORKERS="${TLC_WORKERS:-auto}"

TLC_CASES=(
  "SerializedServiceCorrect|SerializedService|SerializedServiceCorrect.cfg|0|"
  "UartTxControl|UartTxControl|UartTxControl.cfg|0|"
  "UartTxControlLiveness|UartTxControl|UartTxControlLiveness.cfg|0|"
  "UartHardwareControl|UartHardwareControl|UartHardwareControl.cfg|0|"
  "UartHardwareControlLiveness|UartHardwareControl|UartHardwareControlLiveness.cfg|0|"
  "UartTxStartWindow|UartTxStartWindow|UartTxStartWindow.cfg|0|"
  "UartTxPipeline|UartTxPipeline|UartTxPipeline.cfg|0|"
  "UartTxPipelineLiveness|UartTxPipeline|UartTxPipelineLiveness.cfg|0|"
  "UartRxSpsc|UartRxSpsc|UartRxSpsc.cfg|0|"
  "UartRxSpscLiveness|UartRxSpsc|UartRxSpscLiveness.cfg|0|"
  "UartTxConfigDrain|UartTxConfigDrain|UartTxConfigDrain.cfg|0|"
  "UartTxConfigDrainLiveness|UartTxConfigDrain|UartTxConfigDrainLiveness.cfg|0|"
  "SerializedServiceBroken|SerializedService|SerializedServiceBroken.cfg|12|${SERVICE_BROKEN_SIGNATURE}"
  "UartTxControlEphemeralRetry|UartTxControl|UartTxControlEphemeralRetry.cfg|12|${EPHEMERAL_RETRY_SIGNATURE}"
  "UartTxControlMissingSelfDispatch|UartTxControl|UartTxControlMissingSelfDispatch.cfg|12|${MISSING_SELF_DISPATCH_SIGNATURE}"
  "UartTxControlBoundSubmitContext|UartTxControl|UartTxControlBoundSubmitContext.cfg|12|${BOUND_SUBMIT_CONTEXT_SIGNATURE}"
  "UartTxControlMovingBoundary|UartTxControl|UartTxControlMovingBoundary.cfg|13|${MOVING_BOUNDARY_SIGNATURE}"
  "UartTxControlSubmitAfterTake|UartTxControl|UartTxControlSubmitAfterTake.cfg|12|${SUBMIT_AFTER_TAKE_SIGNATURE}"
  "UartTxControlOwnerRetake|UartTxControl|UartTxControlOwnerRetake.cfg|12|${OWNER_RETAKE_SIGNATURE}"
  "UartHardwareControlNoRemask|UartHardwareControl|UartHardwareControlNoRemask.cfg|12|${NO_REMASK_SIGNATURE}"
  "UartHardwareControlLateStatus|UartHardwareControl|UartHardwareControlLateStatus.cfg|12|${LATE_STATUS_SIGNATURE}"
  "UartHardwareControlEarlyQuiesce|UartHardwareControl|UartHardwareControlEarlyQuiesce.cfg|12|${EARLY_QUIESCE_SIGNATURE}"
  "UartHardwareControlAbortABA|UartHardwareControl|UartHardwareControlAbortABA.cfg|12|${ABORT_ABA_SIGNATURE}"
  "UartTxStartWindowEarlyMutation|UartTxStartWindow|UartTxStartWindowEarlyMutation.cfg|12|${START_WINDOW_EARLY_MUTATION_SIGNATURE}"
  "UartTxStartWindowDoubleTerminal|UartTxStartWindow|UartTxStartWindowDoubleTerminal.cfg|12|${START_WINDOW_DOUBLE_TERMINAL_SIGNATURE}"
  "UartTxStartWindowMissingFailureConfig|UartTxStartWindow|UartTxStartWindowMissingFailureConfig.cfg|12|${START_WINDOW_MISSING_FAILURE_CONFIG_SIGNATURE}"
  "UartTxStartWindowStartedPreCommitWitness|UartTxStartWindow|UartTxStartWindowStartedPreCommitWitness.cfg|12|${START_WINDOW_STARTED_PRECOMMIT_WITNESS_SIGNATURE}"
  "UartTxStartWindowFailedConfigPreCommitWitness|UartTxStartWindow|UartTxStartWindowFailedConfigPreCommitWitness.cfg|12|${START_WINDOW_FAILED_CONFIG_PRECOMMIT_WITNESS_SIGNATURE}"
  "UartTxStartWindowFailedCallbackPreConfigWitness|UartTxStartWindow|UartTxStartWindowFailedCallbackPreConfigWitness.cfg|12|${START_WINDOW_FAILED_CALLBACK_PRECONFIG_WITNESS_SIGNATURE}"
  "UartTxPipelineEarlyPromote|UartTxPipeline|UartTxPipelineEarlyPromote.cfg|12|${PIPELINE_EARLY_PROMOTE_SIGNATURE}"
  "UartTxPipelineTerminalDoubleFinish|UartTxPipeline|UartTxPipelineTerminalDoubleFinish.cfg|12|${PIPELINE_DOUBLE_FINISH_SIGNATURE}"
  "UartTxPipelineRetryConsume|UartTxPipeline|UartTxPipelineRetryConsume.cfg|12|${PIPELINE_RETRY_CONSUME_SIGNATURE}"
  "UartTxPipelineWrongCompletion|UartTxPipeline|UartTxPipelineWrongCompletion.cfg|13|${PIPELINE_WRONG_COMPLETION_SIGNATURE}"
  "UartTxPipelineOwnerPublication|UartTxPipeline|UartTxPipelineOwnerPublication.cfg|13|${PIPELINE_OWNER_PUBLICATION_SIGNATURE}"
  "UartRxSpscMovingBoundary|UartRxSpsc|UartRxSpscMovingBoundary.cfg|12|${RX_MOVING_BOUNDARY_SIGNATURE}"
  "UartRxSpscStaleReleaseEvent|UartRxSpsc|UartRxSpscStaleReleaseEvent.cfg|12|${RX_STALE_RELEASE_EVENT_SIGNATURE}"
  "UartTxConfigDrainMovingBoundary|UartTxConfigDrain|UartTxConfigDrainMovingBoundary.cfg|12|${CONFIG_DRAIN_MOVING_BOUNDARY_SIGNATURE}"
  "UartTxConfigDrainPendingPayload|UartTxConfigDrain|UartTxConfigDrainPendingPayload.cfg|12|${CONFIG_DRAIN_PENDING_PAYLOAD_SIGNATURE}"
)

REJECTED_TLC_FAILURE_PATTERN='Parse Error|Semantic errors?:|TLC Bug:|Exception in thread|OutOfMemoryError|StackOverflowError|^java\..*(Exception|Error):|^Error: TLC (encountered|threw|was unable|cannot|could not)|^Error: The configuration file|^Error: Cannot find source file'

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cache_root="${XDG_CACHE_HOME:-${HOME:-/tmp}/.cache}"
jar_path="${TLA2TOOLS_JAR:-${cache_root}/libxr/tla-plus/${TLA_VERSION}/tla2tools.jar}"
run_root="$(mktemp -d "${TMPDIR:-/tmp}/libxr-tlc.XXXXXX")"
download_path=""

cleanup() {
  rm -rf -- "${run_root}"
  if [[ -n "${download_path}" ]]; then
    rm -f -- "${download_path}"
  fi
}
trap cleanup EXIT

verify_config_allowlist() {
  local -a expected_configs=()
  local -a config_paths=()
  local -a actual_configs=()
  local spec name module config expected_status expected_text path
  local expected_sorted actual_sorted unique_count missing unexpected

  for spec in "${TLC_CASES[@]}"; do
    IFS='|' read -r name module config expected_status expected_text <<<"${spec}"
    expected_configs+=("${config}")
  done

  unique_count="$(printf '%s\n' "${expected_configs[@]}" | LC_ALL=C sort -u | wc -l)"
  unique_count="${unique_count//[[:space:]]/}"
  if [[ "${unique_count}" -ne "${#expected_configs[@]}" ]]; then
    echo "Duplicate TLC configs in runner allowlist" >&2
    return 1
  fi

  shopt -s nullglob
  config_paths=("${script_dir}"/*.cfg)
  shopt -u nullglob
  for path in "${config_paths[@]}"; do
    actual_configs+=("${path##*/}")
  done

  expected_sorted="$(printf '%s\n' "${expected_configs[@]}" | LC_ALL=C sort)"
  actual_sorted="$(printf '%s\n' "${actual_configs[@]}" | LC_ALL=C sort)"
  if [[ "${expected_sorted}" == "${actual_sorted}" ]]; then
    return 0
  fi

  missing="$(comm -23 \
    <(printf '%s\n' "${expected_configs[@]}" | LC_ALL=C sort) \
    <(printf '%s\n' "${actual_configs[@]}" | LC_ALL=C sort))"
  unexpected="$(comm -13 \
    <(printf '%s\n' "${expected_configs[@]}" | LC_ALL=C sort) \
    <(printf '%s\n' "${actual_configs[@]}" | LC_ALL=C sort))"
  echo "TLC config allowlist mismatch." >&2
  echo "Missing: ${missing:-<none>}" >&2
  echo "Unexpected: ${unexpected:-<none>}" >&2
  return 1
}

sha256_file() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print tolower($1)}'
  elif command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print tolower($1)}'
  else
    echo "Neither sha256sum nor shasum is available" >&2
    return 1
  fi
}

download_jar() {
  mkdir -p "$(dirname "${jar_path}")"
  download_path="${jar_path}.download.$$"
  echo "Downloading pinned TLA+ v${TLA_VERSION} tools..."
  if command -v curl >/dev/null 2>&1; then
    curl -fL --retry 3 --output "${download_path}" "${JAR_URL}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${download_path}" "${JAR_URL}"
  else
    echo "Neither curl nor wget is available" >&2
    return 1
  fi
  local downloaded_hash=""
  if ! downloaded_hash="$(sha256_file "${download_path}")"; then
    echo "Unable to calculate downloaded jar SHA-256" >&2
    return 1
  fi
  if [[ "${downloaded_hash}" != "${JAR_SHA256}" ]]; then
    echo "Downloaded jar SHA-256 mismatch: ${downloaded_hash}" >&2
    return 1
  fi
  mv -- "${download_path}" "${jar_path}"
  download_path=""
}

command -v java >/dev/null 2>&1 || {
  echo "java is required" >&2
  exit 1
}

verify_config_allowlist

if [[ ! -f "${jar_path}" ]]; then
  download_jar
fi
actual_hash="$(sha256_file "${jar_path}")"
if [[ "${actual_hash}" != "${JAR_SHA256}" ]]; then
  echo "Pinned jar SHA-256 mismatch at '${jar_path}': ${actual_hash}" >&2
  exit 1
fi

version_output="$(java -cp "${jar_path}" tlc2.TLC -help 2>&1 || true)"
if ! grep -Eq "Version[[:space:]]+${TLC_VERSION}([^0-9]|$)" <<<"${version_output}"; then
  echo "Expected TLC ${TLC_VERSION} in the pinned TLA+ v${TLA_VERSION} jar" >&2
  exit 1
fi

cd "${script_dir}"

run_case() {
  local name="$1"
  local module="$2"
  local config="$3"
  local expected_status="$4"
  local expected_text="$5"
  local case_workers="1"
  local output status expected_error error_count

  if [[ "${expected_status}" -eq 0 ]]; then
    case_workers="${WORKERS}"
  fi
  echo
  echo "== TLC: ${name} =="
  set +e
  output="$(java -XX:+UseParallelGC -cp "${jar_path}" tlc2.TLC \
    -cleanup -metadir "${run_root}/${name}" -workers "${case_workers}" \
    -config "${config}" "${module}" 2>&1)"
  status=$?
  set -e
  printf '%s\n' "${output}"

  if [[ "${status}" -ne "${expected_status}" ]]; then
    echo "${name} exited ${status}; expected ${expected_status}" >&2
    return 1
  fi
  if grep -Eq "${REJECTED_TLC_FAILURE_PATTERN}" <<<"${output}"; then
    echo "${name} reported a parser, semantic, or TLC internal error" >&2
    return 1
  fi

  if [[ "${expected_status}" -eq 0 ]]; then
    if [[ -n "${expected_text}" ]]; then
      echo "${name} has an invalid success-case failure signature" >&2
      return 1
    fi
    if ! grep -Fxq "Model checking completed. No error has been found." \
        <<<"${output}"; then
      echo "${name} exited zero without TLC's successful completion marker" >&2
      return 1
    fi
    echo "PASS: ${name}"
    return 0
  fi

  if [[ -z "${expected_text}" ]]; then
    echo "${name} has no expected counterexample signature" >&2
    return 1
  fi
  expected_error="Error: ${expected_text}."
  error_count="$(grep -c '^Error:' <<<"${output}" || true)"
  if [[ "${error_count}" -ne 2 ]] ||
      ! grep -Fxq "${expected_error}" <<<"${output}" ||
      ! grep -Fxq "Error: The behavior up to this point is:" <<<"${output}"; then
    echo "${name} did not produce only the expected counterexample '${expected_error}'" >&2
    return 1
  fi
  echo "PASS: ${name} produced the expected counterexample"
}

for spec in "${TLC_CASES[@]}"; do
  IFS='|' read -r name module config expected_status expected_text <<<"${spec}"
  run_case "${name}" "${module}" "${config}" "${expected_status}" "${expected_text}"
done

echo
echo "All TLC expectations passed."
