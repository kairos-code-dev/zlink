#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"
COMPARE_SH="${SCRIPT_DIR}/run_stream_compare.sh"

STACK="zlink"
SIZE=1024
CCU=1000
DURATION=3
REPEATS=1
INFLIGHT=1
CLIENT_IO_THREADS=1
SERVER_IO_THREADS=1
RECORD=0
RESULT_DIR=""

usage()
{
    cat <<'USAGE'
Usage:
  profile_stream_hotspots.sh [options]

Options:
  --stack <name>                stack name (default: zlink)
  --size <64|1024|65536>        payload size (default: 1024)
  --ccu <N>                     client connections (default: 1000)
  --duration <sec>              duration seconds (default: 3)
  --repeats <N>                 repeats (default: 1)
  --inflight <N>                inflight per conn (default: 1)
  --client-io-threads <N>       client io threads (default: 1)
  --server-io-threads <N>       server io threads (default: 1)
  --record                      run perf record in addition to perf stat
  --result-dir <dir>            output dir
  -h, --help

If perf is missing, this script falls back to running benchmark only and
writes a skip status file.
USAGE
}

validate_int()
{
    local name="$1"
    local value="$2"
    if ! [[ "${value}" =~ ^[0-9]+$ ]] || (( value < 1 )); then
        echo "invalid ${name}: ${value}" >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stack)
            STACK="${2:-}"
            shift 2
            ;;
        --size)
            SIZE="${2:-}"
            shift 2
            ;;
        --ccu)
            CCU="${2:-}"
            shift 2
            ;;
        --duration)
            DURATION="${2:-}"
            shift 2
            ;;
        --repeats)
            REPEATS="${2:-}"
            shift 2
            ;;
        --inflight)
            INFLIGHT="${2:-}"
            shift 2
            ;;
        --client-io-threads)
            CLIENT_IO_THREADS="${2:-}"
            shift 2
            ;;
        --server-io-threads)
            SERVER_IO_THREADS="${2:-}"
            shift 2
            ;;
        --record)
            RECORD=1
            shift
            ;;
        --result-dir)
            RESULT_DIR="${2:-}"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

validate_int "--size" "${SIZE}"
validate_int "--ccu" "${CCU}"
validate_int "--duration" "${DURATION}"
validate_int "--repeats" "${REPEATS}"
validate_int "--inflight" "${INFLIGHT}"
validate_int "--client-io-threads" "${CLIENT_IO_THREADS}"
validate_int "--server-io-threads" "${SERVER_IO_THREADS}"

if [[ -z "${RESULT_DIR}" ]]; then
    RESULT_DIR="${ROOT_DIR}/doc/plan/baseline/$(date +%Y%m%d)_phase05_profile_${STACK}${SIZE}"
fi

mkdir -p "${RESULT_DIR}"
BENCH_RESULT_DIR="${RESULT_DIR}/benchmark"

BENCH_CMD=(
    "${COMPARE_SH}"
    --stack "${STACK}"
    --size "${SIZE}"
    --ccu "${CCU}"
    --duration "${DURATION}"
    --repeats "${REPEATS}"
    --inflight "${INFLIGHT}"
    --client-io-threads "${CLIENT_IO_THREADS}"
    --server-io-threads "${SERVER_IO_THREADS}"
)

{
    echo "#!/usr/bin/env bash"
    echo "set -euo pipefail"
    echo "RESULT_DIR=\"${BENCH_RESULT_DIR}\" \\"
    printf '  '
    printf '%q ' "${BENCH_CMD[@]}"
    echo
} >"${RESULT_DIR}/command.sh"
chmod +x "${RESULT_DIR}/command.sh"

if ! command -v perf >/dev/null 2>&1; then
    {
        echo "status=skipped"
        echo "reason=perf_not_found"
        echo "timestamp=$(date +'%F %T')"
    } >"${RESULT_DIR}/profile_status.txt"
    echo "[PROFILE] perf not found. running benchmark without profiling."
    RESULT_DIR="${BENCH_RESULT_DIR}" "${BENCH_CMD[@]}"
    echo "[PROFILE] benchmark-only run complete result_dir=${BENCH_RESULT_DIR}"
    exit 0
fi

echo "[PROFILE] perf stat start"
RESULT_DIR="${BENCH_RESULT_DIR}" perf stat -d -d -d \
    -o "${RESULT_DIR}/perf_stat.txt" \
    -- "${BENCH_CMD[@]}"

if [[ "${RECORD}" == "1" ]]; then
    PERF_DATA="${RESULT_DIR}/perf.data"
    echo "[PROFILE] perf record start"
    RESULT_DIR="${RESULT_DIR}/benchmark_record" perf record \
        -F 99 \
        -g \
        -o "${PERF_DATA}" \
        -- "${BENCH_CMD[@]}"
    perf report --stdio -i "${PERF_DATA}" --sort dso,symbol \
        >"${RESULT_DIR}/perf_report.txt" || true
fi

{
    echo "status=ok"
    echo "timestamp=$(date +'%F %T')"
} >"${RESULT_DIR}/profile_status.txt"

echo "[PROFILE] complete result_dir=${RESULT_DIR}"
