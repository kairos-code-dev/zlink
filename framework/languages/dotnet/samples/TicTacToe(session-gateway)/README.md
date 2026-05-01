# Tic Tac Toe Session Actor Dispatch Sample

This directory contains the session actor dispatch version of the TicTacToe sample.

The existing TicTacToe sample in `../TicTacToe` is the baseline sample and must
remain unchanged except for required public API name updates.

The sample is a separate project and uses real framework routed-channel and
session actor dispatch APIs:

- embedded registry and framework `UseDiscovery(...)` based service and routed-channel discovery
- an API server reached through a normal framework channel for authentication and match creation
- a Play channel server reached by the API server for match room creation
- routed channel registration and client calls
- Session server remote actor creation through `CreateRemoteActorAsync(...)`
- Session server actor dispatch through `DispatchToActorAsync(...)`
- Play actor typed request handlers
- Play actor client notifications through `SessionProxy`
- registry discovery metadata adapter for session location bind, resolve, and stale unbind guard
- request/reply matching by request sequence
- reconnect recovery for the same `actorId` through a second Session server
- a complete two-actor TicTacToe round ending in `player-x` winning

Code is split by runtime role:

- `Configuration/` defines sample names, timeouts, and generated endpoints.
- `Infrastructure/` creates the embedded registry and sample registry metadata backed
  session location store.
- `Api/` owns actor authentication and match creation relay.
- `Session/` owns the STREAM session and actor binding. The scenario
  starts two Session servers to verify reconnect across Session nodes.
- `Play/` owns TicTacToe match state, Play channel room creation, and client-facing notify.
- `Client/` owns typed stream connector requests and notify handlers.
- `Scenario/` wires the two hosts and the client together.
- `Contracts/` contains packets shared by the client, session server, and play server.

Run it with:

```bash
/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionActorDispatch.csproj" -c Debug
```

Do not use an in-memory routed-channel replacement to make this sample pass.
Service channel clients and routed peers are discovered through the registry.
