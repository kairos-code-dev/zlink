# Tic Tac Toe Sample

This sample maps a two-player tic-tac-toe flow onto `Zlink.Framework`:

1. the client calls the API server over HTTP,
2. the API server asks the play server to create a game over a ZLink channel,
3. the API server returns the play endpoint and game id,
4. two clients connect to the play STREAM endpoint,
5. each client sends `AuthenticateReq` to the play server,
6. the play session asks the API server to authenticate over the `Api` channel,
7. the API server returns `actorId`, and the play session creates a play actor whose `ActorId` is that `actorId`,
8. both actors join the same tic-tac-toe game,
9. the actors send `PlaceMarkReq` packets until player X wins,
10. the game pushes `PlayerJoinedNotify` to the waiting opponent when the second player joins,
11. the game pushes `GameStateNotify` after joins and moves,
12. the game SPOT runs a timer that marks the current player as timed out if a turn takes too long.

Packet type names use `Req` for request packets, `Res` for response packets,
and `Notify` for server push packets.

TicTacToe uses JSON payloads for STREAM, channel, actor, and room Spot
messages.

The sample is grouped by its own solution:

```bash
dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln
```

Run the sample smoke path:

```bash
framework/languages/dotnet/samples/TicTacToe/run_sample.sh
```

On Windows PowerShell:

```powershell
.\framework\languages\dotnet\samples\TicTacToe\run_sample.ps1
```

The standalone client lives in [`Client`](Client). Use it when you want to
read or run just the client side of the flow. `Program` reads the client options
and runs `TicTacToeClientScenario`; the scenario calls HTTP `POST /games`, reads
the returned Play endpoint, creates the two stream connectors, and then verifies
authentication, joins, moves, and pushes.
The request, response, and push DTOs live in [`Shared`](Shared) so the
server and client use the same protocol contract. The reusable client flow lives
in [`Client`](Client); the sample runner starts the server roles and then
runs that client as the self-check.

The server executable also supports separate roles:

```bash
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Server -- play --config ./appsettings.json
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Server -- api --config ./appsettings.json
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Client
```

The server reads `Sample` settings from the config file through
`Microsoft.Extensions.Configuration`. The runner writes a temporary
`appsettings.json`, starts the `play` and `api` roles with `--config`, waits for
their endpoints, and then runs the standalone client.
