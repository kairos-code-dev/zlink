#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UPSTREAM_DIR="${SCRIPT_DIR}/upstream"
BUILD_DIR="${UPSTREAM_DIR}/build-local"
UUID_STUB_DIR="${BUILD_DIR}/uuid_stub"
LOG_DIR="${SCRIPT_DIR}/result"
UPSTREAM_URL="${CPPSERVER_UPSTREAM_URL:-https://github.com/chronoxor/CppServer.git}"
UPSTREAM_REF="${CPPSERVER_UPSTREAM_REF:-}"

MODE="${MODE:-echo}"            # echo | multicast
ADDRESS="${ADDRESS:-127.0.0.1}"
PORT="${PORT:-27410}"
THREADS="${THREADS:-32}"
CLIENTS="${CLIENTS:-10000}"
MESSAGES="${MESSAGES:-30}"
SIZE="${SIZE:-1024}"
DURATION="${DURATION:-10}"

if [[ ! -f "${UPSTREAM_DIR}/CMakeLists.txt" ]]; then
  rm -rf "${UPSTREAM_DIR}"
  git clone --depth 1 "${UPSTREAM_URL}" "${UPSTREAM_DIR}" >/dev/null
  if [[ -n "${UPSTREAM_REF}" ]]; then
    git -C "${UPSTREAM_DIR}" fetch --depth 1 origin "${UPSTREAM_REF}" >/dev/null
    git -C "${UPSTREAM_DIR}" checkout "${UPSTREAM_REF}" >/dev/null
  fi
fi

mkdir -p "${BUILD_DIR}" "${LOG_DIR}" "${UUID_STUB_DIR}/uuid"

if [[ ! -f "/usr/include/uuid/uuid.h" && ! -f "/usr/local/include/uuid/uuid.h" ]]; then
  cat >"${UUID_STUB_DIR}/uuid/uuid.h" <<'EOF'
#ifndef UUID_UUID_H
#define UUID_UUID_H

#include <stdint.h>
#include <stdlib.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t uuid_t[16];

static inline void uuid_generate_time(uuid_t out)
{
    static int seeded = 0;
    if (!seeded) {
        seeded = 1;
        srand((unsigned int)time(NULL));
    }
    for (int i = 0; i < 16; ++i)
        out[i] = (uint8_t)(rand() & 0xFF);
    out[6] = (uint8_t)((out[6] & 0x0F) | 0x10);
    out[8] = (uint8_t)((out[8] & 0x3F) | 0x80);
}

static inline void uuid_generate_random(uuid_t out)
{
    uuid_generate_time(out);
}

#ifdef __cplusplus
}
#endif

#endif
EOF
fi

if [[ "${MODE}" == "echo" ]]; then
  SERVER_TARGET="cppserver-performance-tcp_echo_server"
  CLIENT_TARGET="cppserver-performance-tcp_echo_client"
  SERVER_BIN="${BUILD_DIR}/${SERVER_TARGET}"
  CLIENT_BIN="${BUILD_DIR}/${CLIENT_TARGET}"
elif [[ "${MODE}" == "multicast" ]]; then
  SERVER_TARGET="cppserver-performance-tcp_multicast_server"
  CLIENT_TARGET="cppserver-performance-tcp_multicast_client"
  SERVER_BIN="${BUILD_DIR}/${SERVER_TARGET}"
  CLIENT_BIN="${BUILD_DIR}/${CLIENT_TARGET}"
else
  echo "Unsupported MODE='${MODE}'. Use echo or multicast." >&2
  exit 2
fi

cmake -S "${UPSTREAM_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-I${UUID_STUB_DIR}" >/dev/null

cmake --build "${BUILD_DIR}" --target "${SERVER_TARGET}" "${CLIENT_TARGET}" -j"$(nproc)" >/dev/null

ts="$(date +%Y%m%d_%H%M%S)"
server_log="${LOG_DIR}/${ts}_${MODE}_server.log"
client_log="${LOG_DIR}/${ts}_${MODE}_client.log"
control_fifo="${BUILD_DIR}/.${MODE}_server_control.fifo"

cleanup() {
  exec 9>&- 2>/dev/null || true
  rm -f "${control_fifo}" >/dev/null 2>&1 || true

  if [[ -n "${server_pid:-}" ]] && kill -0 "${server_pid}" 2>/dev/null; then
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

rm -f "${control_fifo}"
mkfifo "${control_fifo}"

# Keep server stdin open; CppServer performance servers stop on stdin EOF.
exec 9<>"${control_fifo}"

"${SERVER_BIN}" --port "${PORT}" --threads "${THREADS}" <"${control_fifo}" >"${server_log}" 2>&1 &
server_pid=$!
sleep 1

if [[ "${MODE}" == "echo" ]]; then
  "${CLIENT_BIN}" \
    --address "${ADDRESS}" \
    --port "${PORT}" \
    --threads "${THREADS}" \
    --clients "${CLIENTS}" \
    --messages "${MESSAGES}" \
    --size "${SIZE}" \
    --seconds "${DURATION}" | tee "${client_log}"
else
  "${CLIENT_BIN}" \
    --address "${ADDRESS}" \
    --port "${PORT}" \
    --threads "${THREADS}" \
    --clients "${CLIENTS}" \
    --size "${SIZE}" \
    --seconds "${DURATION}" | tee "${client_log}"
fi

cleanup
trap - EXIT

echo
echo "server_log=${server_log}"
echo "client_log=${client_log}"
