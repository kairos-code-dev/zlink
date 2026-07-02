# Bingo Sample

This sample implements the common Bingo flow from
`framework/doc/framework/common/sample/bingo/README.ko.md`.

Bingo is the Protobuf game sample. Its stream, channel, actor, and room Spot
payloads use the framework Protobuf codec so the multi-server game flow also
shows a schema-oriented binary contract.

Directory layout:

- `Shared/` contains only the public sample DTO and protobuf contracts.
- `Client/` contains the real stream connector client. The scenario creates
  three client connectors: `player-1` connects to Session A, while `player-2`
  and `observer` connect to Session B.
- `Client/Configuration/` contains client-only endpoint and packet settings.
- `Server/Configuration/` contains the server topology, packet names, and
  framework timing settings used by the server roles.
- `Server/Api/` contains player authentication and matching API handlers.
- `Server/Play/` contains player actors, Entry Spot admission, room spots,
  Redis-backed room matching, submitted cards, server-driven draws, automatic
  marks, winner detection, rare reward fan-out over Spot pub/sub, and
  session-bound push.
- `Server/Session/` contains the stream Session server and actor relay handlers.
- Servers register themselves in a shared location store (the per-run Redis
  container) and auto-connect; no registry process exists.

Each top-level client/shared directory and each server role directory has its
own project file. The root `Bingo.csproj` is the aggregate build entry point for
IDEs and CLI builds. `Bingo.sln` keeps the same layout: `Shared`, `Client`,
`Server/Configuration`, and `Server/<role>`.

Build the whole sample with:

```bash
dotnet build "framework/languages/dotnet/samples/Bingo/Bingo.csproj"
```

Run it with:

```bash
framework/languages/dotnet/samples/Bingo/run_sample.sh
```

On Windows PowerShell:

```powershell
.\framework\languages\dotnet\samples\Bingo\run_sample.ps1
```

The script starts two Api servers, two Play servers, two Session
servers, and a Redis match queue as separate processes. It waits for their
endpoints, waits briefly for local server connections to settle, and then runs
the connector client. The
client flow is self-checking. It fails if the three connectors do not
authenticate as distinct actors, match into one room across Play nodes, observe
the rare reward event from the non-owner Play node, or deliver push
notifications to the bound client sessions. After the game finishes, the server
self-check also verifies that room actors leave the room Spot, return to Entry
Spot, and are destroyed from the Entry Spot context.

The runner always provisions a dedicated Redis Docker container on a
Docker-assigned loopback port, derives `BINGO_REDIS_ENDPOINT` from it, and
removes that container on success or failure, so each run's room-allocation
state stays isolated and never touches a developer's local Redis. Docker is
therefore required to run the sample. The runner also supplies a unique
`BINGO_REDIS_KEY_PREFIX` for each execution so parallel sample runs do not share
Redis match queue keys.
