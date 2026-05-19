# ZLink Framework .NET Samples

| Sample | Purpose |
|--------|---------|
| [TicTacToe](./TicTacToe) | Tic-tac-toe API server, play server, standalone client, game creation, STREAM authentication, actor game join, move requests, and game-state messages. |
| [TicTacToe(session-gateway)](./TicTacToe(session-gateway)/) | Session gateway variant with shared contracts, separate client/server projects, routed actor dispatch, reconnect recovery, and SPOT-backed match rooms. |
| [Bingo(session-gateway)](./Bingo(session-gateway)/) | Matching room sample with four authenticated clients, Entry Spot admission, host-start checks, timer draws, automatic marks, same-sequence winners, and bound-session push notifications. |

Run all samples:

```bash
./framework/languages/dotnet/samples/run_samples.sh
```

## Framework Channel Names

Samples use the typed Framework channel configuration names that match zlink
Discovery auto-connect types:

| API | Meaning |
|-----|---------|
| `AddClientServerChannel` | DEALER clients connect to ROUTER servers. |
| `AddDealerMeshChannel` | DEALER clients form a peer mesh. |
| `AddFanoutChannel` | SUB subscribers connect to PUB publishers. |
| `AddRouteMeshChannel` | ROUTER peers form a route mesh. |
| `AddSpotMesh` | SPOT nodes form a SPOT mesh. |
