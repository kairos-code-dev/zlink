#!/usr/bin/env bash

start_redis_container() {
  local name="$1"
  shift
  local create_output create_status start_output start_status candidate running
  set +e
  create_output="$(timeout -k 2s 10s docker create --name "${name}" "$@" 2>&1)"
  create_status="$?"
  set -e
  candidate="$(printf '%s\n' "${create_output}" | awk '/^[0-9a-f]{12,64}$/ { print; exit }')"
  if [[ "${create_status}" != "0" || -z "${candidate}" ]]; then
    printf 'Failed to create Redis container %s (docker status %s)\n%s\n' "${name}" "${create_status}" "${create_output}" >&2
    return 1
  fi
  REDIS_CONTAINER_ID="${candidate}"
  set +e
  start_output="$(timeout -k 2s 10s docker start "${candidate}" 2>&1)"
  start_status="$?"
  set -e
  running="$(timeout -k 2s 5s docker inspect -f '{{.State.Running}}' "${candidate}" 2>/dev/null || true)"
  if [[ "${running}" == "true" ]]; then
    return 0
  fi
  timeout -k 2s 10s docker rm -f "${candidate}" >/dev/null 2>&1 || true
  REDIS_CONTAINER_ID=""
  printf 'Failed to start Redis container %s (docker status %s)\n%s\n' "${name}" "${start_status}" "${start_output}" >&2
  return 1
}
