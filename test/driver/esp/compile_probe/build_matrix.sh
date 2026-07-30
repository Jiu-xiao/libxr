#!/usr/bin/env bash
set -uo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
repo_dir="$(cd "${project_dir}/../../../.." && pwd -P)"
fifo_source="${repo_dir}/driver/esp/esp_uart_fifo.cpp"
build_root="${MATRIX_BUILD_ROOT:-${project_dir}/build}"
jobs="${MATRIX_JOBS:-2}"

matrix_cases=(
  "esp32|default|1|sdkconfig.defaults"
  "esp32s2|default|0|sdkconfig.defaults"
  "esp32c2|default|0|sdkconfig.defaults"
  "esp32c3|default|0|sdkconfig.defaults"
  "esp32c5|default|0|sdkconfig.defaults"
  "esp32c6|default|0|sdkconfig.defaults"
  "esp32c61|default|0|sdkconfig.defaults"
  "esp32h2|default|0|sdkconfig.defaults"
  "esp32s3|default|1|sdkconfig.defaults"
  "esp32s3|unicore|0|sdkconfig.unicore.defaults"
  "esp32p4|default|1|sdkconfig.defaults"
)

usage() {
  cat <<'EOF'
Usage: ./build_matrix.sh [case ...]

With no cases, run all 11 target/variant combinations. A default variant is named by
its target (for example, esp32s3); the negative control is esp32s3-unicore.

Environment:
  MATRIX_BUILD_ROOT  Result/build root (default: <project>/build)
  MATRIX_JOBS        Parallel build jobs (default: 2)
EOF
}

case_id_for() {
  local target="$1"
  local variant="$2"
  if [[ "${variant}" == "default" ]]; then
    printf '%s\n' "${target}"
  else
    printf '%s-%s\n' "${target}" "${variant}"
  fi
}

select_cases() {
  local requested spec target variant expected defaults case_id matched

  if (($# == 0)); then
    selected_cases=("${matrix_cases[@]}")
    return 0
  fi

  selected_cases=()
  for requested in "$@"; do
    if [[ "${requested}" == "-h" || "${requested}" == "--help" ]]; then
      usage
      exit 0
    fi

    matched=0
    for spec in "${matrix_cases[@]}"; do
      IFS='|' read -r target variant expected defaults <<<"${spec}"
      case_id="$(case_id_for "${target}" "${variant}")"
      if [[ "${requested}" == "${case_id}" ]]; then
        selected_cases+=("${spec}")
        matched=1
        break
      fi
    done
    if ((matched == 0)); then
      printf 'Unknown matrix case: %s\n' "${requested}" >&2
      usage >&2
      return 2
    fi
  done
}

run_logged() {
  local log_file="$1"
  shift
  {
    printf '[COMMAND]'
    printf ' %q' "$@"
    printf '\n'
  } >>"${log_file}"
  "$@" >>"${log_file}" 2>&1
}

read_s3_unicore_setting() {
  local sdkconfig="$1"
  if grep -Fqx 'CONFIG_FREERTOS_UNICORE=y' "${sdkconfig}"; then
    printf '1\n'
    return 0
  fi
  if grep -Fqx '# CONFIG_FREERTOS_UNICORE is not set' "${sdkconfig}"; then
    printf '0\n'
    return 0
  fi
  return 1
}

validate_s3_config() {
  local variant="$1"
  local sdkconfig="$2"
  local log_file="$3"
  local expected_unicore=0
  local actual_unicore

  if [[ "${variant}" == "unicore" ]]; then
    expected_unicore=1
  fi
  if [[ ! -f "${sdkconfig}" ]]; then
    printf '[CONFIG] missing sdkconfig: %s\n' "${sdkconfig}" >>"${log_file}"
    return 1
  fi
  if ! grep -Fqx 'CONFIG_IDF_TARGET="esp32s3"' "${sdkconfig}"; then
    printf '[CONFIG] expected CONFIG_IDF_TARGET="esp32s3" in %s\n' \
      "${sdkconfig}" >>"${log_file}"
    return 1
  fi
  if ! actual_unicore="$(read_s3_unicore_setting "${sdkconfig}")"; then
    printf '[CONFIG] CONFIG_FREERTOS_UNICORE has no explicit value in %s\n' \
      "${sdkconfig}" >>"${log_file}"
    return 1
  fi

  printf '[CONFIG] target=esp32s3 variant=%s sdkconfig=%s ' \
    "${variant}" "${sdkconfig}" >>"${log_file}"
  printf 'CONFIG_FREERTOS_UNICORE=%s expected=%s\n' \
    "${actual_unicore}" "${expected_unicore}" >>"${log_file}"
  [[ "${actual_unicore}" == "${expected_unicore}" ]]
}

resolve_fifo_object() {
  local compile_commands="$1"
  local source_file="$2"
  python - "${compile_commands}" "${source_file}" <<'PY'
import json
import os
import shlex
import sys


def absolute(path, directory):
    if not os.path.isabs(path):
        path = os.path.join(directory, path)
    return os.path.realpath(path)


database_path, source_path = sys.argv[1:]
source_path = os.path.realpath(source_path)
try:
    with open(database_path, encoding="utf-8") as database_file:
        database = json.load(database_file)
except (OSError, json.JSONDecodeError) as error:
    print(f"[EVIDENCE] cannot read {database_path}: {error}", file=sys.stderr)
    sys.exit(1)

matches = []
for entry in database:
    directory = entry.get("directory", "")
    source = entry.get("file")
    if source and absolute(source, directory) == source_path:
        matches.append(entry)

if len(matches) != 1:
    print(
        f"[EVIDENCE] expected one compile_commands entry for {source_path}, "
        f"found {len(matches)}",
        file=sys.stderr,
    )
    sys.exit(1)

entry = matches[0]
directory = entry.get("directory", "")
output = entry.get("output")
if not output:
    arguments = entry.get("arguments")
    if arguments is None:
        try:
            arguments = shlex.split(entry["command"])
        except (KeyError, ValueError) as error:
            print(f"[EVIDENCE] cannot parse compile command: {error}", file=sys.stderr)
            sys.exit(1)
    for index, argument in enumerate(arguments):
        if argument == "-o" and index + 1 < len(arguments):
            output = arguments[index + 1]
            break
        if argument.startswith("-o") and len(argument) > 2:
            output = argument[2:]
            break

if not output:
    print("[EVIDENCE] FIFO compile command has no output path", file=sys.stderr)
    sys.exit(1)

print(absolute(output, directory))
PY
}

find_unique_artifact() {
  local root="$1"
  local pattern="$2"
  local recursive="$3"
  local label="$4"
  local -a matches=()

  if [[ ! -d "${root}" ]]; then
    printf '[EVIDENCE] %s search root is missing: %s\n' "${label}" "${root}" >&2
    return 1
  fi
  if [[ "${recursive}" == "1" ]]; then
    mapfile -d '' -t matches < <(find "${root}" -type f -name "${pattern}" -print0)
  else
    mapfile -d '' -t matches < <(
      find "${root}" -mindepth 1 -maxdepth 1 -type f -name "${pattern}" -print0
    )
  fi
  if ((${#matches[@]} != 1)); then
    printf '[EVIDENCE] expected one %s under %s, found %d\n' \
      "${label}" "${root}" "${#matches[@]}" >&2
    return 1
  fi
  printf '%s\n' "${matches[0]}"
}

measure_artifact() {
  local file="$1"
  local size hash_line
  size="$(stat -c '%s' -- "${file}")" || return 1
  hash_line="$(sha256sum -- "${file}")" || return 1
  printf '%s\t%s\n' "${size}" "${hash_line%% *}"
}

read_cmake_file_tool() {
  local cache_file="$1"
  local variable="$2"
  python - "${cache_file}" "${variable}" <<'PY'
import sys

cache_path, variable = sys.argv[1:]
prefix = f"{variable}:FILEPATH="
with open(cache_path, encoding="utf-8") as cache_file:
    matches = [line.rstrip("\n").removeprefix(prefix) for line in cache_file
               if line.startswith(prefix)]
if len(matches) != 1 or not matches[0]:
    print(f"[EVIDENCE] expected one {prefix} entry, found {len(matches)}",
          file=sys.stderr)
    sys.exit(1)
print(matches[0])
PY
}

append_summary_row() {
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$@" >>"${summary_file}"
}

validate_summary() {
  python - "${summary_file}" "${#selected_cases[@]}" <<'PY'
import csv
import sys

summary_path = sys.argv[1]
expected_rows = int(sys.argv[2])
with open(summary_path, newline="", encoding="utf-8") as summary_file:
    rows = list(csv.reader(summary_file, delimiter="\t"))

if not rows or len(rows[0]) != 15:
    print("[EVIDENCE] summary header must contain 15 columns", file=sys.stderr)
    sys.exit(1)
if len(rows) != expected_rows + 1:
    print(
        f"[EVIDENCE] expected {expected_rows} summary rows, found {len(rows) - 1}",
        file=sys.stderr,
    )
    sys.exit(1)
for row_number, row in enumerate(rows[1:], start=2):
    if len(row) != 15:
        print(
            f"[EVIDENCE] summary row {row_number} has {len(row)} columns",
            file=sys.stderr,
        )
        sys.exit(1)
PY
}

run_case() {
  local spec="$1"
  local target variant expected defaults_name case_id defaults
  local build_dir sdkconfig_path log_file relative_log
  local result="FAIL" exit_code=1 duration_s start_s end_s
  local fifo_object="" elf_path="" libxr_path="" cmake_nm="" evidence
  local fifo_object_bytes="NA" fifo_object_sha256="NA"
  local fifo_elf_symbols="NA"
  local elf_bytes="NA" elf_sha256="NA"
  local libxr_bytes="NA" libxr_sha256="NA"
  local evidence_failed=0

  IFS='|' read -r target variant expected defaults_name <<<"${spec}"
  case_id="$(case_id_for "${target}" "${variant}")"
  defaults="${project_dir}/${defaults_name}"
  build_dir="${targets_root}/${case_id}"
  sdkconfig_path="${build_dir}/sdkconfig"
  log_file="${logs_root}/${case_id}.log"
  relative_log="logs/${case_id}.log"

  mkdir -p "${build_dir}"
  : >"${log_file}"
  start_s="$(date +%s)"
  printf '[ESP_UART_COMPILE_PROBE] target=%s variant=%s state=CONFIGURING\n' \
    "${target}" "${variant}"

  run_logged "${log_file}" idf.py -C "${project_dir}" -B "${build_dir}" \
    "-DSDKCONFIG=${sdkconfig_path}" "-DSDKCONFIG_DEFAULTS=${defaults}" \
    "-DLIBXR_ESP_UART_EXPECT_IRQ_SERIALIZATION=${expected}" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON set-target "${target}"
  exit_code=$?

  if ((exit_code == 0)) && [[ "${target}" == "esp32s3" ]]; then
    validate_s3_config "${variant}" "${sdkconfig_path}" "${log_file}"
    exit_code=$?
  fi

  if ((exit_code == 0)); then
    printf '[ESP_UART_COMPILE_PROBE] target=%s variant=%s state=BUILDING\n' \
      "${target}" "${variant}"
    run_logged "${log_file}" cmake --build "${build_dir}" --parallel "${jobs}"
    exit_code=$?
  fi

  if ((exit_code == 0)); then
    fifo_object="$(
      resolve_fifo_object "${build_dir}/compile_commands.json" "${fifo_source}" \
        2>>"${log_file}"
    )" || evidence_failed=1
    if [[ -n "${fifo_object}" && -f "${fifo_object}" ]]; then
      evidence="$(measure_artifact "${fifo_object}" 2>>"${log_file}")" || evidence_failed=1
      if [[ -n "${evidence}" ]]; then
        fifo_object_bytes="${evidence%%$'\t'*}"
        fifo_object_sha256="${evidence#*$'\t'}"
      fi
    else
      printf '[EVIDENCE] FIFO object is missing: %s\n' "${fifo_object:-NA}" \
        >>"${log_file}"
      evidence_failed=1
    fi

    elf_path="$(
      find_unique_artifact "${build_dir}" '*.elf' 0 'application ELF' \
        2>>"${log_file}"
    )" || evidence_failed=1
    if [[ -n "${elf_path}" ]]; then
      evidence="$(measure_artifact "${elf_path}" 2>>"${log_file}")" || evidence_failed=1
      if [[ -n "${evidence}" ]]; then
        elf_bytes="${evidence%%$'\t'*}"
        elf_sha256="${evidence#*$'\t'}"
      fi

      cmake_nm="$(
        read_cmake_file_tool "${build_dir}/CMakeCache.txt" CMAKE_NM 2>>"${log_file}"
      )" || evidence_failed=1
      if [[ -n "${cmake_nm}" && -x "${cmake_nm}" ]]; then
        fifo_elf_symbols="$(
          "${cmake_nm}" --defined-only -C "${elf_path}" |
            grep -F -c 'LibXR::ESP32UartFifo::ESP32UartFifo(' || true
        )"
        printf '[EVIDENCE] fifo_elf_symbols=%s nm=%s\n' \
          "${fifo_elf_symbols}" "${cmake_nm}" >>"${log_file}"
        if [[ ! "${fifo_elf_symbols}" =~ ^[1-9][0-9]*$ ]]; then
          evidence_failed=1
        fi
      else
        printf '[EVIDENCE] CMAKE_NM is missing or not executable: %s\n' \
          "${cmake_nm:-NA}" >>"${log_file}"
        evidence_failed=1
      fi
    fi

    libxr_path="$(
      find_unique_artifact "${build_dir}/libxr_build" 'libxr.a' 1 'libxr archive' \
        2>>"${log_file}"
    )" || evidence_failed=1
    if [[ -n "${libxr_path}" ]]; then
      evidence="$(measure_artifact "${libxr_path}" 2>>"${log_file}")" || evidence_failed=1
      if [[ -n "${evidence}" ]]; then
        libxr_bytes="${evidence%%$'\t'*}"
        libxr_sha256="${evidence#*$'\t'}"
      fi
    fi

    if ((evidence_failed == 0)); then
      result="PASS"
    else
      exit_code=1
    fi
  fi

  end_s="$(date +%s)"
  duration_s=$((end_s - start_s))
  append_summary_row "${result}" "${target}" "${variant}" "${expected}" \
    "${exit_code}" "${duration_s}" "${fifo_object_bytes}" \
    "${fifo_object_sha256}" "${fifo_elf_symbols}" "${elf_bytes}" "${elf_sha256}" \
    "${libxr_bytes}" "${libxr_sha256}" "${fifo_source_sha256}" "${relative_log}"

  printf '[ESP_UART_COMPILE_PROBE] target=%s variant=%s state=%s log=%s\n' \
    "${target}" "${variant}" "${result}" "${log_file}"
  if [[ "${result}" != "PASS" ]]; then
    overall_result=1
  fi
}

for command_name in idf.py cmake python sha256sum stat find grep; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    printf 'Required command is unavailable: %s\n' "${command_name}" >&2
    exit 2
  fi
done
if [[ ! "${jobs}" =~ ^[1-9][0-9]*$ ]]; then
  printf 'MATRIX_JOBS must be a positive integer, got: %s\n' "${jobs}" >&2
  exit 2
fi
if [[ ! -f "${fifo_source}" ]]; then
  printf 'FIFO source is missing: %s\n' "${fifo_source}" >&2
  exit 2
fi

select_cases "$@" || exit $?
mkdir -p "${build_root}"
build_root="$(cd "${build_root}" && pwd -P)"
targets_root="${build_root}/targets"
logs_root="${build_root}/logs"
summary_file="${build_root}/summary.tsv"
mkdir -p "${targets_root}" "${logs_root}"

fifo_source_hash_line="$(sha256sum -- "${fifo_source}")" || exit 2
fifo_source_sha256="${fifo_source_hash_line%% *}"
printf 'result\ttarget\tvariant\texpected_irq_serialization\texit_code\tduration_s\t' \
  >"${summary_file}"
printf 'fifo_object_bytes\tfifo_object_sha256\tfifo_elf_symbols\telf_bytes\telf_sha256\t' \
  >>"${summary_file}"
printf 'libxr_bytes\tlibxr_sha256\tfifo_source_sha256\tlog\n' >>"${summary_file}"

overall_result=0
for spec in "${selected_cases[@]}"; do
  run_case "${spec}"
done

if ! validate_summary; then
  overall_result=1
fi

printf '[ESP_UART_COMPILE_PROBE] summary=%s\n' "${summary_file}"
exit "${overall_result}"
