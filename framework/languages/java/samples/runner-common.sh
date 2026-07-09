#!/usr/bin/env bash

zlink_sample_descendants() {
  local pid="$1"
  local child
  (pgrep -P "${pid}" 2>/dev/null || true) | while read -r child; do
    zlink_sample_descendants "${child}"
    echo "${child}"
  done
}

descendants() {
  zlink_sample_descendants "$@"
}

zlink_sample_print_logs() {
  local status="$1"
  if declare -F print_logs >/dev/null 2>&1; then
    print_logs "${status}"
  fi
}

cleanup() {
  local status="$?"
  set +e
  zlink_sample_print_logs "${status}"
  local pid_list_name=""
  if declare -p pids >/dev/null 2>&1; then
    pid_list_name="pids"
  elif declare -p PIDS >/dev/null 2>&1; then
    pid_list_name="PIDS"
  fi
  if [[ -n "${pid_list_name}" ]]; then
    local -n zlink_sample_pids="${pid_list_name}"
    for ((i=${#zlink_sample_pids[@]}-1; i>=0; i--)); do
      local pid="${zlink_sample_pids[$i]}"
      for child in $(zlink_sample_descendants "${pid}"); do
        kill "${child}" >/dev/null 2>&1 || true
      done
      kill "${pid}" >/dev/null 2>&1 || true
    done
    local any_alive=1
    for _ in $(seq 1 "${ZLINK_SAMPLE_CLEANUP_WAIT_ATTEMPTS:-100}"); do
      any_alive=0
      for pid in "${zlink_sample_pids[@]}"; do
        if kill -0 "${pid}" >/dev/null 2>&1; then
          any_alive=1
          break
        fi
        for child in $(zlink_sample_descendants "${pid}"); do
          if kill -0 "${child}" >/dev/null 2>&1; then
            any_alive=1
            break
          fi
        done
      done
      if [[ "${any_alive}" == "0" ]]; then
        break
      fi
      sleep 0.1
    done
    if [[ "${any_alive}" == "1" ]]; then
      for ((i=${#zlink_sample_pids[@]}-1; i>=0; i--)); do
        local pid="${zlink_sample_pids[$i]}"
        for child in $(zlink_sample_descendants "${pid}"); do
          kill -9 "${child}" >/dev/null 2>&1 || true
        done
        kill -9 "${pid}" >/dev/null 2>&1 || true
      done
    fi
    set +m
    for pid in "${zlink_sample_pids[@]}"; do
      wait "${pid}" >/dev/null 2>&1 || true
    done
  fi
  if [[ -n "${redis_container_id:-}" ]]; then
    docker rm -f "${redis_container_id}" >/dev/null 2>&1 || true
  elif [[ -n "${REDIS_CONTAINER:-}" ]]; then
    docker rm -f "${REDIS_CONTAINER}" >/dev/null 2>&1 || true
  fi
  return "${status}"
}

wait_port() {
  local host="$1"
  local port="${2:-}"
  if [[ "${port}" == tcp://* ]]; then
    local endpoint="${port#tcp://}"
    host="${endpoint%:*}"
    port="${endpoint##*:}"
  elif [[ "${port}" == http://* ]]; then
    local endpoint="${port#http://}"
    host="${endpoint%:*}"
    port="${endpoint##*:}"
  elif [[ "${port}" == redis://* ]]; then
    local endpoint="${port#redis://}"
    host="${endpoint%:*}"
    port="${endpoint##*:}"
  elif [[ "${host}" == tcp://* ]]; then
    local endpoint="${host#tcp://}"
    host="${endpoint%:*}"
    port="${endpoint##*:}"
  elif [[ "${host}" == http://* ]]; then
    local endpoint="${host#http://}"
    host="${endpoint%:*}"
    port="${endpoint##*:}"
  elif [[ "${host}" == redis://* ]]; then
    local endpoint="${host#redis://}"
    host="${endpoint%:*}"
    port="${endpoint##*:}"
  elif [[ "${host}" == *:* && -z "${port}" ]]; then
    port="${host##*:}"
    host="${host%:*}"
  fi
  local deadline=$((SECONDS + ${ZLINK_SAMPLE_WAIT_SECONDS:-60}))
  while (( SECONDS < deadline )); do
    if (echo >"/dev/tcp/${host}/${port}") >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${host}:${port}" >&2
  return 1
}

wait_http() {
  local url="$1"
  local deadline=$((SECONDS + ${ZLINK_SAMPLE_WAIT_SECONDS:-60}))
  while (( SECONDS < deadline )); do
    if curl -fsS "${url}/health" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  echo "Timed out waiting for ${url}/health" >&2
  return 1
}

wait_http_health() {
  wait_http "$@"
}

gradle_run() {
  if declare -p ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS >/dev/null 2>&1; then
    ../../gradlew "${ZLINK_SAMPLE_GRADLE_SETTINGS_ARGS[@]}" --no-daemon "$@" --quiet
  else
    ../../gradlew --settings-file standalone.settings.gradle.kts --no-daemon "$@" --quiet
  fi
}

app_bin() {
  local project="$1"
  local script="$2"
  echo "${project}/build/install/${script}/bin/${script}"
}

zlink_redis_cleanup_scope() {
  local scope="$1"
  if [[ -z "${scope}" ]]; then
    return 0
  fi
  while read -r container_id container_name; do
    if [[ "${container_name}" == "${scope}"* ]]; then
      docker rm -f "${container_id}" >/dev/null 2>&1 || true
    fi
  done < <(docker ps -a --format '{{.ID}} {{.Names}}')
}

zlink_redis_wait_ready() {
  local container_id="$1"
  local timeout_seconds="${ZLINK_REDIS_READY_TIMEOUT_SECONDS:-60}"
  local deadline=$((SECONDS + timeout_seconds))

  while (( SECONDS < deadline )); do
    if timeout -k 2s 5s docker exec "${container_id}" redis-cli ping 2>/dev/null | grep -q '^PONG$'; then
      return 0
    fi
    sleep 1
  done

  printf 'Timed out waiting for Redis container %s to answer PING\n' "${container_id}" >&2
  return 1
}

zlink_redis_start_scoped() {
  local scope="$1"
  local image="${2:-${ZLINK_REDIS_IMAGE:-redis:7.2-alpine}}"
  local port_mapping="${3:-127.0.0.1::6379}"
  local docker_timeout_seconds="${ZLINK_REDIS_DOCKER_TIMEOUT_SECONDS:-10}"
  local run_id="${ZLINK_REDIS_RUN_ID:-$$}"
  local name="${scope}-${run_id}-${BASHPID}-${RANDOM}"

  local create_output create_status container_id start_output start_status running
  set +e
  create_output="$(timeout -k 2s "${docker_timeout_seconds}s" docker create \
    --name "${name}" \
    -p "${port_mapping}" \
    "${image}" 2>&1)"
  create_status="$?"
  set -e
  container_id="$(printf '%s\n' "${create_output}" | awk '/^[0-9a-f]{12,64}$/ { print; exit }')"
  if [[ "${create_status}" != "0" || -z "${container_id}" ]]; then
    printf 'Failed to create Redis container %s (docker status %s)\n%s\n' "${name}" "${create_status}" "${create_output}" >&2
    return 1
  fi
  set +e
  start_output="$(timeout -k 2s "${docker_timeout_seconds}s" docker start "${container_id}" 2>&1)"
  start_status="$?"
  set -e
  running="$(timeout -k 2s 5s docker inspect -f '{{.State.Running}}' "${container_id}" 2>/dev/null || true)"
  if [[ "${running}" != "true" ]]; then
    docker rm -f "${container_id}" >/dev/null 2>&1 || true
    printf 'Failed to start Redis container %s (docker status %s)\n%s\n' "${name}" "${start_status}" "${start_output}" >&2
    return 1
  fi
  if ! zlink_redis_wait_ready "${container_id}"; then
    docker rm -f "${container_id}" >/dev/null 2>&1 || true
    return 1
  fi
  printf '%s' "${container_id}"
}

zlink_redis_host_port() {
  local container_id="$1"
  local host_port
  host_port="$(timeout -k 2s 5s docker inspect \
    -f '{{(index (index .NetworkSettings.Ports "6379/tcp") 0).HostPort}}' \
    "${container_id}" 2>/dev/null || true)"
  if [[ -z "${host_port}" ]]; then
    printf 'Failed to inspect Redis host port for container %s\n' "${container_id}" >&2
    return 1
  fi
  printf '%s\n' "${host_port}"
}

zlink_redis_endpoint() {
  local container_id="$1"
  local host_port
  host_port="$(zlink_redis_host_port "${container_id}")"
  printf '127.0.0.1:%s\n' "${host_port}"
}

zlink_redis_start_scoped_assign() {
  local container_var="$1"
  local host_port_var="$2"
  shift 2

  local container_id host_port
  if ! container_id="$(zlink_redis_start_scoped "$@")"; then
    return 1
  fi
  if ! host_port="$(zlink_redis_host_port "${container_id}")"; then
    docker rm -f "${container_id}" >/dev/null 2>&1 || true
    return 1
  fi

  printf -v "${container_var}" '%s' "${container_id}"
  printf -v "${host_port_var}" '%s' "${host_port}"
}
