#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
BUILD_DIR="${ROOT_DIR}/core/build"

ZLINK_BIN="${BUILD_DIR}/bin/test_scenario_router_router_zlink_connect"
ZMQ_SRC="${SCRIPT_DIR}/zmq/libzmq_native_router_router_bench.cpp"
ZMQ_BIN="${SCRIPT_DIR}/zmq/bin/libzmq_native_router_router_bench"
LIBZMQ_INCLUDE="${LIBZMQ_INCLUDE:-/tmp/libzmq-native-v435/include}"
LIBZMQ_LIBDIR="${LIBZMQ_LIBDIR:-/tmp/libzmq-native-v435/libzmq-linux-x64}"

RESULT_ROOT="${SCRIPT_DIR}/result"
LOG_DIR="${LOG_DIR:-${RESULT_ROOT}/diff_check_$(date +%Y%m%d_%H%M%S)}"
CSV_FILE="${LOG_DIR}/metrics.csv"
SUMMARY_FILE="${LOG_DIR}/SUMMARY.txt"

REPEATS="${REPEATS:-5}"
SIZE="${SIZE:-1024}"
CCU="${CCU:-10000}"
INFLIGHT="${INFLIGHT:-10}"
DURATION="${DURATION:-4}"
SENDERS="${SENDERS:-1}"
HWM="${HWM:-1000000}"
WARMUP_MODE="${WARMUP_MODE:-stable}"
RATIO_LIMIT="${RATIO_LIMIT:-3.0}"
ABS_LIMIT_MS="${ABS_LIMIT_MS:-5}"
BASE_PORT="${BASE_PORT:-33000}"

mkdir -p "${LOG_DIR}" "$(dirname "${ZMQ_BIN}")"

build_runners() {
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DZLINK_BUILD_TESTS=ON >/dev/null
  cmake --build "${BUILD_DIR}" --target test_scenario_router_router_zlink_connect -j"$(nproc)"

  if [[ ! -f "${LIBZMQ_INCLUDE}/zmq.h" ]]; then
    local fallback_include="${ROOT_DIR}/core/bench/benchwithzmq/libzmq/libzmq_dist/linux-x64/include"
    if [[ -f "${fallback_include}/zmq.h" ]]; then
      LIBZMQ_INCLUDE="${fallback_include}"
    fi
  fi

  if [[ ! -f "${LIBZMQ_LIBDIR}/libzmq.so" ]]; then
    local fallback_libdir="${ROOT_DIR}/core/bench/benchwithzmq/libzmq/libzmq_dist/linux-x64/lib"
    if [[ -f "${fallback_libdir}/libzmq.so" ]]; then
      LIBZMQ_LIBDIR="${fallback_libdir}"
    fi
  fi

  if [[ ! -x "${ZMQ_BIN}" || "${ZMQ_SRC}" -nt "${ZMQ_BIN}" ]]; then
    g++ -O2 -std=c++11 "${ZMQ_SRC}" \
      -I"${LIBZMQ_INCLUDE}" \
      -L"${LIBZMQ_LIBDIR}" \
      -Wl,-rpath,"${LIBZMQ_LIBDIR}" \
      -lzmq -lpthread -o "${ZMQ_BIN}"
  fi

  export LD_LIBRARY_PATH="${LIBZMQ_LIBDIR}:${BUILD_DIR}/lib:${LD_LIBRARY_PATH:-}"
}

metric_from_log() {
  local key="$1"
  local log="$2"
  awk -v k="${key}" '
    /^METRIC / {
      for (i = 2; i <= NF; ++i) {
        split($i, kv, "=");
        if (kv[1] == k) {
          print kv[2];
          exit;
        }
      }
    }
  ' "${log}"
}

append_csv_row() {
  local lib="$1"
  local self="$2"
  local run="$3"
  local log="$4"
  local rc="$5"

  local ready_wait_ms
  local first_connected_ms
  local first_connection_ready_ms
  local connect_to_ready_ms
  local connected
  local connection_ready
  local connect_delayed
  local connect_delayed_zero
  local connect_delayed_nonzero
  local connect_retried
  local disconnected
  local handshake_failed
  local throughput_msg_s

  ready_wait_ms="$(metric_from_log ready_wait_ms "${log}")"
  first_connected_ms="$(metric_from_log first_connected_ms "${log}")"
  first_connection_ready_ms="$(metric_from_log first_connection_ready_ms "${log}")"
  connect_to_ready_ms="$(metric_from_log connect_to_ready_ms "${log}")"
  connected="$(metric_from_log connected "${log}")"
  connection_ready="$(metric_from_log connection_ready "${log}")"
  connect_delayed="$(metric_from_log connect_delayed "${log}")"
  connect_delayed_zero="$(metric_from_log connect_delayed_zero "${log}")"
  connect_delayed_nonzero="$(metric_from_log connect_delayed_nonzero "${log}")"
  connect_retried="$(metric_from_log connect_retried "${log}")"
  disconnected="$(metric_from_log disconnected "${log}")"
  handshake_failed="$(metric_from_log handshake_failed "${log}")"
  throughput_msg_s="$(metric_from_log throughput_msg_s "${log}")"

  if [[ -z "${ready_wait_ms}" ]]; then
    ready_wait_ms="nan"
    first_connected_ms="nan"
    first_connection_ready_ms="nan"
    connect_to_ready_ms="nan"
    connected="nan"
    connection_ready="nan"
    connect_delayed="nan"
    connect_delayed_zero="nan"
    connect_delayed_nonzero="nan"
    connect_retried="nan"
    disconnected="nan"
    handshake_failed="nan"
    throughput_msg_s="nan"
    if [[ "${rc}" -eq 0 ]]; then
      rc=99
    fi
  fi

  printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n" \
    "${lib}" "${self}" "${run}" "${ready_wait_ms}" "${first_connected_ms}" \
    "${first_connection_ready_ms}" "${connect_to_ready_ms}" "${connected}" \
    "${connection_ready}" "${connect_delayed}" "${connect_delayed_zero}" \
    "${connect_delayed_nonzero}" "${connect_retried}" "${disconnected}" \
    "${handshake_failed}" "${throughput_msg_s}" "${rc}" >>"${CSV_FILE}"
}

run_one() {
  local lib="$1"
  local self="$2"
  local run="$3"
  local port_base="$4"
  local bin

  if [[ "${lib}" == "zlink" ]]; then
    bin="${ZLINK_BIN}"
  else
    bin="${ZMQ_BIN}"
  fi

  local play_port="${port_base}"
  local api_port="$((port_base + 101))"
  local log="${LOG_DIR}/${lib}_self${self}_run${run}.log"
  local warmup_flag=""

  case "${WARMUP_MODE}" in
    none)
      warmup_flag="--warmup-none"
      ;;
    legacy)
      warmup_flag="--warmup-legacy"
      ;;
    stable)
      warmup_flag=""
      ;;
    *)
      echo "invalid WARMUP_MODE=${WARMUP_MODE} (expected stable|legacy|none)" >&2
      exit 2
      ;;
  esac

  echo "===== ${lib} self=${self} run=${run} play=${play_port} api=${api_port} ====="

  set +e
  "${bin}" \
    --self-connect "${self}" \
    --size "${SIZE}" \
    --ccu "${CCU}" \
    --inflight "${INFLIGHT}" \
    --duration "${DURATION}" \
    --senders "${SENDERS}" \
    --hwm "${HWM}" \
    --play-port "${play_port}" \
    --api-port "${api_port}" \
    ${warmup_flag:+${warmup_flag}} | tee "${log}"
  local rc="${PIPESTATUS[0]}"
  set -e

  append_csv_row "${lib}" "${self}" "${run}" "${log}" "${rc}"
}

median_col() {
  local lib="$1"
  local self="$2"
  local col="$3"
  awk -F',' -v lib="${lib}" -v self="${self}" -v col="${col}" '
    NR > 1 && $1 == lib && $2 == self && $17 == 0 && $col != "nan" { print $col }
  ' "${CSV_FILE}" | sort -n | awk '
    { a[++n] = $1 }
    END {
      if (n == 0) {
        print "nan";
        exit;
      }
      if (n % 2 == 1)
        print a[(n + 1) / 2];
      else
        print (a[n / 2] + a[n / 2 + 1]) / 2;
    }
  '
}

sum_col() {
  local lib="$1"
  local self="$2"
  local col="$3"
  awk -F',' -v lib="${lib}" -v self="${self}" -v col="${col}" '
    NR > 1 && $1 == lib && $2 == self && $col != "nan" { s += $col }
    END { print s + 0 }
  ' "${CSV_FILE}"
}

ratio_or_nan() {
  local lhs="$1"
  local rhs="$2"
  awk -v lhs="${lhs}" -v rhs="${rhs}" '
    BEGIN {
      if (lhs == "nan" || rhs == "nan" || rhs <= 0) {
        print "nan";
      } else {
        printf "%.3f\n", lhs / rhs;
      }
    }
  '
}

gt_limit() {
  local value="$1"
  local limit="$2"
  awk -v value="${value}" -v limit="${limit}" '
    BEGIN {
      if (value == "nan")
        exit 1;
      exit(value > limit ? 0 : 1);
    }
  '
}

build_runners

cat >"${CSV_FILE}" <<'EOF'
lib,self,run,ready_wait_ms,first_connected_ms,first_connection_ready_ms,connect_to_ready_ms,connected,connection_ready,connect_delayed,connect_delayed_zero,connect_delayed_nonzero,connect_retried,disconnected,handshake_failed,throughput_msg_s,rc
EOF

port_seed="${BASE_PORT}"
for self in 0 1; do
  for run in $(seq 1 "${REPEATS}"); do
    run_one "zlink" "${self}" "${run}" "${port_seed}"
    port_seed=$((port_seed + 20))
    run_one "zmq" "${self}" "${run}" "${port_seed}"
    port_seed=$((port_seed + 20))
  done
done

overall_rc=0
failed_runs="$(awk -F',' 'NR > 1 && $17 != 0 { c++ } END { print c + 0 }' "${CSV_FILE}")"
if [[ "${failed_runs}" -gt 0 ]]; then
  overall_rc=1
fi

{
  echo "connect diff check"
  echo "log_dir=${LOG_DIR}"
  echo "repeats=${REPEATS} size=${SIZE} ccu=${CCU} inflight=${INFLIGHT} duration=${DURATION} senders=${SENDERS} hwm=${HWM} warmup=${WARMUP_MODE}"
  echo "ratio_limit=${RATIO_LIMIT} abs_limit_ms=${ABS_LIMIT_MS}"
  echo "failed_runs=${failed_runs}"
  echo
} >"${SUMMARY_FILE}"

for self in 0 1; do
  z_ready_median="$(median_col zlink "${self}" 4)"
  m_ready_median="$(median_col zmq "${self}" 4)"
  z_ctr_median="$(median_col zlink "${self}" 7)"
  m_ctr_median="$(median_col zmq "${self}" 7)"

  ready_ratio="$(ratio_or_nan "${z_ready_median}" "${m_ready_median}")"
  ctr_ratio="$(ratio_or_nan "${z_ctr_median}" "${m_ctr_median}")"

  z_retry_sum="$(sum_col zlink "${self}" 13)"
  z_disc_sum="$(sum_col zlink "${self}" 14)"
  z_hs_fail_sum="$(sum_col zlink "${self}" 15)"
  m_retry_sum="$(sum_col zmq "${self}" 13)"
  m_disc_sum="$(sum_col zmq "${self}" 14)"
  m_hs_fail_sum="$(sum_col zmq "${self}" 15)"

  {
    echo "[self=${self}]"
    echo "  zlink median ready_wait_ms=${z_ready_median} connect_to_ready_ms=${z_ctr_median}"
    echo "  zmq   median ready_wait_ms=${m_ready_median} connect_to_ready_ms=${m_ctr_median}"
    echo "  ratio ready_wait_ms(zlink/zmq)=${ready_ratio}"
    echo "  ratio connect_to_ready_ms(zlink/zmq)=${ctr_ratio}"
    echo "  zlink events: retried_sum=${z_retry_sum} disconnected_sum=${z_disc_sum} handshake_failed_sum=${z_hs_fail_sum}"
    echo "  zmq   events: retried_sum=${m_retry_sum} disconnected_sum=${m_disc_sum} handshake_failed_sum=${m_hs_fail_sum}"
    echo
  } >>"${SUMMARY_FILE}"

  if gt_limit "${ready_ratio}" "${RATIO_LIMIT}" || gt_limit "${ctr_ratio}" "${RATIO_LIMIT}"; then
    overall_rc=1
  fi

  if [[ "${ctr_ratio}" == "nan" ]]; then
    if awk -v zmq_ctr="${m_ctr_median}" -v zlink_ctr="${z_ctr_median}" -v lim="${ABS_LIMIT_MS}" '
      BEGIN {
        if (zmq_ctr == "nan" || zlink_ctr == "nan")
          exit 1;
        if (zmq_ctr <= 0 && zlink_ctr > lim)
          exit 0;
        exit 1;
      }
    '; then
      overall_rc=1
    fi
  fi

  if awk -v zsum="${z_retry_sum}" -v zdisc="${z_disc_sum}" -v zhs="${z_hs_fail_sum}" '
    BEGIN {
      if (zsum > 0 || zdisc > 0 || zhs > 0)
        exit 0;
      exit 1;
    }
  '; then
    overall_rc=1
  fi
done

if [[ "${overall_rc}" -eq 0 ]]; then
  echo "overall=PASS (ratio_limit=${RATIO_LIMIT})" >>"${SUMMARY_FILE}"
else
  echo "overall=FAIL (ratio_limit=${RATIO_LIMIT})" >>"${SUMMARY_FILE}"
fi

cat "${SUMMARY_FILE}"
exit "${overall_rc}"
