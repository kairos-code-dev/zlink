#!/usr/bin/env bash
# Scenario E2E runner. Brings up the shared scenario server (one or more copies,
# in the mode the scenario declares) and then runs the client scenario against it,
# exactly like a sample's run_sample.sh. Usage:
#   run_scenario_e2e.sh            # run every scenario in the catalog
#   run_scenario_e2e.sh CH-002     # run a single scenario
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="${ROOT_DIR}/Zlink.Framework.ScenarioE2E.csproj"
DLL="${ROOT_DIR}/bin/Debug/net8.0/Zlink.Framework.ScenarioE2E.dll"
CHANNEL="scenario-api"

pick_port() {
  python3 - <<'PY'
import socket
s = socket.socket()
s.bind(("127.0.0.1", 0))
print(s.getsockname()[1])
s.close()
PY
}

endpoint_port() {
  local endpoint="$1"
  endpoint="${endpoint#tcp://}"
  endpoint="${endpoint#http://}"
  echo "${endpoint##*:}"
}

wait_port() {
  local endpoint="$1"
  local port
  port="$(endpoint_port "${endpoint}")"
  for _ in $(seq 1 100); do
    if (echo >"/dev/tcp/127.0.0.1/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "timed out waiting for ${endpoint}" >&2
  return 1
}

join_by_comma() {
  local IFS=,
  echo "$*"
}

run_scenario() {
  local id="$1"
  local work_dir
  work_dir="$(mktemp -d)"
  local log_dir="${work_dir}/logs"
  local report="${work_dir}/report.json"
  mkdir -p "${log_dir}"

  local pids=()
  cleanup() {
    set +e
    for pid in "${pids[@]}"; do kill "${pid}" 2>/dev/null; done
    sleep 0.2
    for pid in "${pids[@]}"; do kill -9 "${pid}" 2>/dev/null; done
    for pid in "${pids[@]}"; do wait "${pid}" 2>/dev/null; done
  }
  trap cleanup RETURN

  local mode="" servers="" client_rid=""
  while IFS= read -r line; do
    case "${line}" in
      mode=*) mode="${line#mode=}" ;;
      servers=*) servers="${line#servers=}" ;;
      client=*) client_rid="${line#client=}" ;;
    esac
  done < <(dotnet "${DLL}" topology "${id}")

  local server_names=()
  IFS=',' read -ra server_names <<<"${servers}"

  local zlink_eps=() http_eps=()
  local index name ready_file
  for name in "${server_names[@]}"; do
    zlink_eps+=("tcp://127.0.0.1:$(pick_port)")
    http_eps+=("http://127.0.0.1:$(pick_port)")
  done

  if [[ "${mode}" == "route-mesh" ]]; then
    local client_ep="tcp://127.0.0.1:$(pick_port)"
    for index in "${!server_names[@]}"; do
      name="${server_names[${index}]}"
      ready_file="${work_dir}/${name}.ready"
      # Each peer connects back to the client node and to the other peers.
      local route_peers=("${client_ep}")
      local other
      for other in "${!server_names[@]}"; do
        [[ "${other}" != "${index}" ]] && route_peers+=("${zlink_eps[${other}]}")
      done
      dotnet "${DLL}" server \
        --work-dir "${work_dir}" --server-name "${name}" --mode route-mesh --channel "${CHANNEL}" \
        --zlink-endpoint "${zlink_eps[${index}]}" --http-endpoint "${http_eps[${index}]}" \
        --ready-file "${ready_file}" --route-peer-endpoints "$(join_by_comma "${route_peers[@]}")" \
        >"${log_dir}/server-${name}.log" 2>&1 &
      pids+=("$!")
    done
    for index in "${!server_names[@]}"; do
      wait_port "${zlink_eps[${index}]}"
      wait_port "${http_eps[${index}]}"
    done
    sleep 0.5
    dotnet "${DLL}" client \
      --scenario "${id}" --channel "${CHANNEL}" \
      --client-zlink-endpoint "${client_ep}" --client-routing-id "${client_rid}" \
      --route-peer-endpoints "$(join_by_comma "${zlink_eps[@]}")" \
      --http-endpoint "$(join_by_comma "${http_eps[@]}")" \
      --server-names "${servers}" --report-file "${report}" --log-dir "${log_dir}" \
      >"${log_dir}/client.log" 2>&1
  else
    for index in "${!server_names[@]}"; do
      name="${server_names[${index}]}"
      ready_file="${work_dir}/${name}.ready"
      dotnet "${DLL}" server \
        --work-dir "${work_dir}" --server-name "${name}" --mode channel --channel "${CHANNEL}" \
        --zlink-endpoint "${zlink_eps[${index}]}" --http-endpoint "${http_eps[${index}]}" \
        --ready-file "${ready_file}" \
        >"${log_dir}/server-${name}.log" 2>&1 &
      pids+=("$!")
    done
    for index in "${!server_names[@]}"; do
      wait_port "${zlink_eps[${index}]}"
      wait_port "${http_eps[${index}]}"
    done
    sleep 0.5
    dotnet "${DLL}" client \
      --scenario "${id}" --channel "${CHANNEL}" \
      --zlink-endpoint "$(join_by_comma "${zlink_eps[@]}")" \
      --http-endpoint "$(join_by_comma "${http_eps[@]}")" \
      --server-names "${servers}" --report-file "${report}" --log-dir "${log_dir}" \
      >"${log_dir}/client.log" 2>&1
  fi

  if ! grep -q "scenario-e2e result=passed scenario=${id}" "${log_dir}/client.log"; then
    echo "scenario ${id} FAILED" >&2
    tail -40 "${log_dir}/client.log" >&2 || true
    tail -20 "${log_dir}"/server-*.log >&2 || true
    return 1
  fi
  echo "scenario ${id} passed (report=${report})"
}

dotnet build "${PROJECT}" --maxcpucount:1 >/dev/null

if [[ $# -ge 1 ]]; then
  run_scenario "$1"
else
  while IFS= read -r id; do
    [[ -n "${id}" ]] && run_scenario "${id}"
  done < <(dotnet "${DLL}" list)
fi

echo "scenario-e2e all-passed"
