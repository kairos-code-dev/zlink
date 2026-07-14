# ZLink Framework .NET Samples

| Sample | Purpose |
|--------|---------|
| [TicTacToe](TicTacToe) | Tic-tac-toe sample with two API roles, two Play roles, manual endpoint mapping, Redis room routes, host/guest/observer stream clients, actor game join, move requests, milestone push, and game-state messages. Uses JSON payloads. |
| [Bingo](Bingo) | Matching room sample with four authenticated clients, Entry Spot admission, host-start checks, timer draws, automatic marks, same-sequence winners, and bound-session push notifications. Uses Protobuf payloads. |
| [SupportChat](SupportChat) | Multi-role support conversation sample with API, support, session, registry, probe, and client roles. |
| [ShoppingMall](ShoppingMall) | Order workflow sample with replicated commerce API and order workflow roles. |
| [DeliveryDispatch](DeliveryDispatch) | Delivery dispatch sample with HTTP intake, courier timeout, reassignment, tracking, ZLink fanout, and customer session push. |
| [GameQuest](GameQuest) | Gameplay event and quest mission sample with replicated API and mission roles. |

Run all samples on Linux or WSL:

```bash
./framework/languages/dotnet/samples/run_samples.sh
```

Run all samples on Windows PowerShell:

```powershell
.\framework\languages\dotnet\samples\run_samples.ps1
```

Each sample root owns its executable smoke path through `run_sample.sh` and
`run_sample.ps1`. The runner starts server roles as separate processes, waits
for their endpoints, runs the probe or client self-check, and cleans up the
process tree. Sample server and client projects keep their own role
responsibility; they do not start other sample roles in-process.

Shared sample projects contain only the message contracts that client and server
roles both serialize. Server topology, endpoint names, packet names, and timing
settings belong under `Server/Configuration`. Client and probe settings belong
under their own projects.

## Execution And Configuration

.NET sample runners own process orchestration. They choose free local ports,
start server roles as separate processes, wait for readiness, and then run the
client or probe self-check. Server code reads role-local configuration and then
passes the bound settings into `AddZLinkFramework(...)`.

TicTacToe demonstrates file-backed configuration with
`Microsoft.Extensions.Configuration`: the runner writes temporary role-specific
settings for `api-a`, `api-b`, `play-a`, and `play-b`, and each server role reads
its own file through `--config`. TicTacToe is the manual endpoint sample: it
does not use the framework location store, but it does pass a Redis endpoint and
key prefix to the room route store.

The multi-role samples use role-local configuration classes under
`Server/Configuration` and environment variables supplied by the runner. The
client and probe projects have their own configuration copies because they only
need the endpoints they connect to.

Manual server runs should prefer a config file over long endpoint argument
lists:

```bash
dotnet run --project TicTacToe/Server.Play -- --config ./appsettings.play-a.json
dotnet run --project TicTacToe/Server.Play -- --config ./appsettings.play-b.json
dotnet run --project TicTacToe/Server.Api -- --config ./appsettings.api-a.json
dotnet run --project TicTacToe/Server.Api -- --config ./appsettings.api-b.json
```

The runner remains responsible for process orchestration. Server role code
should only start its own role from the configured settings.

## Framework Channel Names

Samples use the typed Framework channel configuration names that match zlink
Discovery auto-connect types:

| API | Meaning |
|-----|---------|
| `AddClientServerChannel` | DEALER clients connect to ROUTER servers. |
| `AddFanoutChannel` | SUB subscribers connect to PUB publishers. |
| `AddRouteMesh` | ROUTER peers form a route mesh. |
| `AddSpotMesh` | SPOT nodes form a SPOT mesh. |
