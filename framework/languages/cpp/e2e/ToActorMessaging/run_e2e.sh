#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=${ZLINK_CPP_BUILD_DIR:-"$SCRIPT_DIR/../../build"}

"$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_actor"
"$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_caller"
"$BUILD_DIR/zlink_cpp_e2e_to_actor_messaging_client"
