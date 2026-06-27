#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cd "${REPO_ROOT}"

FILE_PATH="${1:-/mnt/synology/apollo/sensor_rgb.record}"
MAX_CHUNKS="${2:-0}"

bazel build //cyber/record:record_perf_reader >/dev/null

run_mode() {
  local mode="$1"
  shift || true
  ./bazel-bin/cyber/record/record_perf_reader \
    --file="${FILE_PATH}" \
    --mode="${mode}" \
    --max_chunks="${MAX_CHUNKS}" \
    "$@"
}

echo "# record_perf_reader begin file=${FILE_PATH} max_chunks=${MAX_CHUNKS}"
run_mode baseline
run_mode uring
run_mode uring_stream
run_mode uring_hysteresis --hwm_mb=500 --lwm_mb=100 --replay_rate=200

if command -v strace >/dev/null 2>&1; then
  strace -c -o /tmp/strace_record_baseline.txt \
    ./bazel-bin/cyber/record/record_perf_reader --file="${FILE_PATH}" --mode=baseline --max_chunks="${MAX_CHUNKS}" >/tmp/record_baseline_result.json
  strace -c -o /tmp/strace_record_uring_stream.txt \
    ./bazel-bin/cyber/record/record_perf_reader --file="${FILE_PATH}" --mode=uring_stream --max_chunks="${MAX_CHUNKS}" >/tmp/record_uring_stream_result.json
  echo "# strace summaries: /tmp/strace_record_baseline.txt /tmp/strace_record_uring_stream.txt"
fi

echo "# record_perf_reader done"
