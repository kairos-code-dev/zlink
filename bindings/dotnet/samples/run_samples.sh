#!/usr/bin/env bash
set -euo pipefail

ROOT="/home/hep7/project/kairos/zlink/bindings/dotnet/samples"

dotnet run --project "$ROOT/PairRecv/PairRecv.csproj"
dotnet run --project "$ROOT/PairCallback/PairCallback.csproj"
dotnet run --project "$ROOT/PubSubRecv/PubSubRecv.csproj"
dotnet run --project "$ROOT/PubSubCallback/PubSubCallback.csproj"
dotnet run --project "$ROOT/DealerRouterRecv/DealerRouterRecv.csproj"
dotnet run --project "$ROOT/DealerRouterCallback/DealerRouterCallback.csproj"
dotnet run --project "$ROOT/StreamRecv/StreamRecv.csproj"
dotnet run --project "$ROOT/StreamCallback/StreamCallback.csproj"
dotnet run --project "$ROOT/SpotRecv/SpotRecv.csproj"
dotnet run --project "$ROOT/SpotCallback/SpotCallback.csproj"
dotnet run --project "$ROOT/RegistryDiscoveryMonitor/RegistryDiscoveryMonitor.csproj"
