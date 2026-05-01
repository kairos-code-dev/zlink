# Tic Tac Toe Session Gateway Sample

This directory contains the session-gateway version of the TicTacToe sample.

The existing TicTacToe sample in `../TicTacToe` is the baseline sample and must
remain unchanged except for required public API name updates.

The sample is a separate project and uses real framework routed-channel and
session-gateway APIs:

- embedded registry and framework `UseDiscovery(...)` based routed-channel discovery
- routed channel registration and client calls
- API server game location lookup over ActorRelay
- Session server `actorId -> stream` binding
- ActorRelay from Session server to Play server
- SessionGateway from Play server to Session server
- request/reply matching by request sequence
- reconnect recovery for the same `actorId` through a second Session server
- a complete two-player TicTacToe round ending in `player-x` winning

Code is split by runtime role:

- `Configuration/` defines sample names, timeouts, and generated endpoints.
- `Infrastructure/` creates the embedded registry used by routed-channel discovery.
- `Api/` owns game creation and in-memory game-to-play-node location lookup.
- `Session/` owns the STREAM session and `actorId -> stream` binding. The scenario
  starts two Session servers to verify reconnect across Session nodes.
- `Play/` owns TicTacToe game state and client-facing notify.
- `Client/` owns typed stream connector requests and notify handlers.
- `Scenario/` wires the two hosts and the client together.
- `Contracts/` contains packets shared by the client, session server, and play server.

Run it with:

```bash
/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Debug
```

Do not use an in-memory routed-channel replacement to make this sample pass.
Do not add manual routed-channel connections to this sample; routed peers are
discovered through the registry.
