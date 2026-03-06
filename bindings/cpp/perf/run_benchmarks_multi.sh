#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
RUNNER="${ROOT_DIR}/bindings/perf/run_policy_bench.py"

ARGS=()
REQUESTED_CLIENTS=""

is_uint() {
    local value="${1:-}"
    [[ "${value}" =~ ^[0-9]+$ ]]
}

ensure_nofile_limit() {
    local clients="${1:-}"
    if [[ "${PERF_SKIP_NOFILE_CHECK:-0}" == "1" ]]; then
        return 0
    fi
    if ! is_uint "${clients}"; then
        return 0
    fi

    local required=$(( clients * 3 + 4096 ))
    local soft
    local hard
    soft="$(ulimit -Sn 2>/dev/null || true)"
    hard="$(ulimit -Hn 2>/dev/null || true)"
    if [[ -z "${soft}" || "${soft}" == "unlimited" ]]; then
        return 0
    fi
    if ! is_uint "${soft}"; then
        return 0
    fi

    local soft_num="${soft}"
    local hard_num=-1
    if [[ "${hard}" == "unlimited" ]]; then
        hard_num=-1
    elif is_uint "${hard}"; then
        hard_num="${hard}"
    fi

    if (( soft_num < required )); then
        local target="${required}"
        if (( hard_num >= 0 && target > hard_num )); then
            target="${hard_num}"
        fi
        if (( target > soft_num )); then
            ulimit -Sn "${target}" 2>/dev/null || true
            soft="$(ulimit -Sn 2>/dev/null || true)"
            if is_uint "${soft}"; then
                soft_num="${soft}"
            fi
        fi
    fi

    if (( soft_num < required )); then
        echo "nofile preflight failed: clients=${clients}, required=${required}, soft=${soft}, hard=${hard}" >&2
        return 1
    fi
    return 0
}

memory_available_kb() {
    if [[ "${PERF_SKIP_MEMORY_CHECK:-0}" == "1" ]]; then
        echo ""
        return
    fi
    if [[ ! -r /proc/meminfo ]]; then
        echo ""
        return
    fi
    awk '/^MemAvailable:/ { print $2; found=1; exit } END { if (!found) print "" }' /proc/meminfo 2>/dev/null || true
}

ensure_memory_budget() {
    local clients="${1:-}"
    if [[ "${PERF_SKIP_MEMORY_CHECK:-0}" == "1" ]]; then
        return 0
    fi
    if ! is_uint "${clients}"; then
        return 0
    fi

    local available_kb
    available_kb="$(memory_available_kb)"
    if ! is_uint "${available_kb}"; then
        return 0
    fi

    local budget_pct="${PERF_MEMORY_BUDGET_PCT:-70}"
    local base_mb="${PERF_MEMORY_BASE_MB:-512}"
    local per_client_kb="${PERF_MEMORY_PER_CLIENT_KB:-1024}"
    if ! is_uint "${budget_pct}" || ! is_uint "${base_mb}" || ! is_uint "${per_client_kb}"; then
        return 0
    fi

    local usable_kb=$(( available_kb * budget_pct / 100 ))
    local base_kb=$(( base_mb * 1024 ))
    local required_kb=$(( base_kb + clients * per_client_kb ))
    if (( usable_kb < required_kb )); then
        echo "memory preflight failed: clients=${clients}, available_kb=${available_kb}, budget_pct=${budget_pct}, base_mb=${base_mb}, per_client_kb=${per_client_kb}" >&2
        return 1
    fi
    return 0
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --clean-build)
            rm -rf "${SCRIPT_DIR}/multi/build"
            shift
            ;;
        --clients)
            REQUESTED_CLIENTS="$2"
            ARGS+=("--multi-clients" "$2")
            shift 2
            ;;
        --warmup)
            ARGS+=("--multi-warmup-seconds" "$2")
            shift 2
            ;;
        --duration)
            ARGS+=("--multi-duration-seconds" "$2")
            shift 2
            ;;
        --hwm)
            ARGS+=("--multi-hwm" "$2")
            shift 2
            ;;
        --sndtimeo|--send-timeout-ms)
            ARGS+=("--multi-sndtimeo-ms" "$2")
            shift 2
            ;;
        --rcvtimeo|--recv-timeout-ms)
            ARGS+=("--multi-rcvtimeo-ms" "$2")
            shift 2
            ;;
        --connect-concurrency)
            ARGS+=("--multi-connect-concurrency" "$2")
            shift 2
            ;;
        --transport-transition-ms)
            ARGS+=("--multi-transport-transition-ms" "$2")
            shift 2
            ;;
        --pattern-transition-ms)
            ARGS+=("--multi-pattern-transition-ms" "$2")
            shift 2
            ;;
        --server-ready-timeout-ms)
            ARGS+=("--multi-server-ready-timeout-ms" "$2")
            shift 2
            ;;
        --server-shutdown-timeout-ms)
            ARGS+=("--multi-server-shutdown-timeout-ms" "$2")
            shift 2
            ;;
        --server-bind-port)
            ARGS+=("--multi-server-bind-port" "$2")
            shift 2
            ;;
        --connect-ready-timeout-ms)
            ARGS+=("--multi-connect-ready-timeout-ms" "$2")
            shift 2
            ;;
        --monitor-hwm)
            ARGS+=("--multi-monitor-hwm" "$2")
            shift 2
            ;;
        *)
            ARGS+=("$1")
            shift
            ;;
    esac
done

PREFLIGHT_CLIENTS="${REQUESTED_CLIENTS:-${PERF_MULTI_CLIENTS:-${PERF_CLIENTS:-100}}}"
if ! ensure_nofile_limit "${PREFLIGHT_CLIENTS}"; then
    exit 1
fi
if ! ensure_memory_budget "${PREFLIGHT_CLIENTS}"; then
    exit 1
fi

exec python3 "${RUNNER}" --binding cpp --suite multi "${ARGS[@]}"
