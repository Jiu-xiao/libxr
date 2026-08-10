#!/usr/bin/env bash

set -eu

build_dir="${1:-build}"
summary="${2:-${build_dir}/test/mspm0_focused_summary.tsv}"
status=0

mkdir -p "$(dirname "${summary}")"
printf 'stage\tresult\n' >"${summary}"

run_stage()
{
  local stage="$1"
  local executable="${build_dir}/test/$2"

  if "${executable}"
  then
    printf '%s\tPASS\n' "${stage}" >>"${summary}"
  else
    printf '%s\tFAIL\n' "${stage}" >>"${summary}"
    status=1
  fi
}

run_stage dispatcher test_mspm0_dma_dispatcher
run_stage dispatcher_external test_mspm0_dma_dispatcher_external
run_stage uart_g3507 test_mspm0_uart_g3507
run_stage uart_g3507_extend test_mspm0_uart_g3507_extend
run_stage uart_g3519 test_mspm0_uart_g3519

cat "${summary}"
exit "${status}"
