#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOTNET_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

dotnet test "${DOTNET_ROOT}/tests/Zlink.Framework.E2ETests/Zlink.Framework.E2ETests.csproj" \
  --filter 'FullyQualifiedName~ActorLifecycleTests.EntrySpot_DestroyActorAsync_Removes_EntryOwned_Actor_Without_Left_Callback' \
  --logger 'console;verbosity=minimal'
echo "dotnet actor lifecycle sample gate completed"

"${SCRIPT_DIR}/TicTacToe/run_sample.sh"
"${SCRIPT_DIR}/Bingo/run_sample.sh"
"${SCRIPT_DIR}/SupportChat/run_sample.sh"
"${SCRIPT_DIR}/ShoppingMall/run_sample.sh"
"${SCRIPT_DIR}/DeliveryDispatch/run_sample.sh"
"${SCRIPT_DIR}/GameQuest/run_sample.sh"
