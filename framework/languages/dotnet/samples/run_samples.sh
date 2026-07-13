#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPT_DIR}/redis-common.sh"

SAMPLES=(TicTacToe Bingo SupportChat ShoppingMall DeliveryDispatch GameQuest)

for scope in \
  zlink-tictactoe-dotnet-redis \
  zlink-bingo-dotnet-redis \
  zlink-supportchat-dotnet-redis \
  zlink-shoppingmall-dotnet-redis \
  zlink-deliverydispatch-dotnet-redis \
  zlink-gamequest-dotnet-redis; do
  zlink_redis_cleanup_scope "${scope}"
done

if (( $# > 0 )); then
  SAMPLES=("$@")
fi

for sample in "${SAMPLES[@]}"; do
  case "${sample}" in
    TicTacToe|Bingo|SupportChat|ShoppingMall|DeliveryDispatch|GameQuest)
      "${SCRIPT_DIR}/${sample}/run_sample.sh"
      ;;
    *)
      echo "Unknown .NET sample '${sample}'." >&2
      exit 2
      ;;
  esac
done
