# Tic Tac Toe Session Actor Dispatch Sample

This directory contains the session actor dispatch version of the TicTacToe sample.
It keeps each runtime role in its own project so the sample topology is easy to
inspect: client, API, Play, and Session are separate assemblies.

The sample is a separate project and uses real framework routed-channel and
session actor dispatch APIs:

- embedded registry and framework `UseDiscovery(...)` based service and routed-channel discovery
- an API server reached through a normal framework channel for authentication and match creation
- a Play channel server reached by the API server for match room creation
- Play server SPOT game rooms created through `IZLinkSpotManager`
- routed channel registration and client calls
- Session server target actor handle creation through `CreateActorHandleAsync(...)`
- Session server actor dispatch through `DispatchToActorAsync(...)`
- Play actor typed request handlers that join and update SPOT game rooms
- Play actor client notifications through `SessionProxy`
- registry discovery metadata adapter for session location bind, resolve, and stale unbind guard
- request/reply matching by request sequence
- reconnect recovery for the same `actorId` through a second Session server
- a complete two-actor TicTacToe round ending in `player-x` winning

Code is split by project:

- `Shared/` contains packets, sample names, and timeouts shared by all roles.
- `Client/` owns typed stream connector requests, notify handlers, reconnect flow,
  and the command-line client executable.
- `Api/` owns actor authentication and match creation relay.
- `Play/` owns the Play channel server, routed actor handlers, and SPOT game rooms.
- `Session/` owns STREAM session binding and actor dispatch into Play.
- `Registry/` owns the embedded registry host.
- `Infrastructure/` owns generated endpoints and the sample registry metadata
  stores shared by Play and Session.
- `Server/` is only the end-to-end scenario executable that starts all server roles
  and runs the client flow.

The scenario stays thin: `Server/Scenario/` wires the runtime topology, and the
actual player actions are delegated to `Client/`.

Run it with:

```bash
/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/Server/TicTacToe.SessionGateway.Server.csproj" -c Debug
```

The client project can also run against an already-started pair of Session stream
endpoints:

```bash
/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/Client/TicTacToe.SessionGateway.Client.csproj" -c Debug -- --stream-endpoint tcp://HOST:PORT --reconnect-stream-endpoint tcp://HOST:PORT
```

Do not use an in-memory routed-channel replacement to make this sample pass.
Service channel clients and routed peers are discovered through the registry.
