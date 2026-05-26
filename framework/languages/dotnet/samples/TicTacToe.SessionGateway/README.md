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
- Session server actor handle binding through `BindActorAsync(...)`
- Session server actor dispatch through `IZLinkSessionActor.RelayAsync(...)`
- Play actor typed request handlers that join and update SPOT game rooms
- Play actor client notifications through `BoundSession`
- ActorGateway-backed session binding and cleanup
- request/reply matching by request sequence
- reconnect recovery for the same `actorId` through a second Session server
- a complete two-actor TicTacToe round ending in `player-x` winning

Code is split by project:

- `Shared/` contains packets, sample names, topology, and timeouts shared by all roles.
- `Client/` owns typed stream connector requests, notify handlers, reconnect flow,
  and the command-line client executable.
- `Server/Api/` owns actor authentication and match creation relay.
- `Server/Play/` owns the Play channel server, routed actor handlers, and SPOT game rooms.
- `Server/Session/` owns STREAM session binding and actor dispatch into Play.
- `Server/Registry/` owns the embedded registry host.

Each server role has its own executable project. The end-to-end sample script
starts Registry, Api, Play, primary Session, and reconnect Session as separate
processes, then runs the connector client against the two Session stream
endpoints.

Build it with:

```bash
dotnet build "framework/languages/dotnet/samples/TicTacToe.SessionGateway/TicTacToe.SessionGateway.csproj"
```

Run it with:

```bash
framework/languages/dotnet/samples/TicTacToe.SessionGateway/run_sample.sh
```

The client project can also run against an already-started pair of Session stream
endpoints:

```bash
dotnet run --project "framework/languages/dotnet/samples/TicTacToe.SessionGateway/Client/TicTacToe.SessionGateway.Client.csproj" -- --stream-endpoint tcp://HOST:PORT --reconnect-stream-endpoint tcp://HOST:PORT
```

Do not use an in-memory routed-channel replacement to make this sample pass.
Service channel clients and routed peers are discovered through the registry.
