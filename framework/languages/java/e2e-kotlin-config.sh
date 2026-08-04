#!/usr/bin/env bash

# Pass E2E configuration through a temporary file instead of exposing the
# process environment as an application configuration interface.
zlink_kotlin_e2e_run() {
  local config_file status name value
  config_file="$(mktemp -t zlink-kotlin-e2e-config.XXXXXX)"
  while IFS='=' read -r name value; do
    case "${name}" in
      ZLINK_KOTLIN_E2E_*)
        local key="${name#ZLINK_KOTLIN_E2E_}"
        key="${key,,}"
        key="${key//_/.}"
        printf 'e2e.%s=%s\n' "${key}" "${value}" >>"${config_file}"
        ;;
    esac
  done < <(env)

  set +e
  "$@" --e2e-config "${config_file}"
  status="$?"
  set -e
  rm -f "${config_file}"
  return "${status}"
}
