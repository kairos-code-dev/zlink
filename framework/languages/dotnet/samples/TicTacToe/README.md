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

TicTacToe is the MessagePack game sample. Its STREAM, channel, actor, and room
Spot payloads use MessagePack to show compact binary packets for a small
real-time game.

The sample is grouped by its own solution:

```bash
dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln
```

Run the all-in-one server smoke path:

```bash
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Server
```

The standalone client lives in [`Client`](./Client). Use it when you want to
read or run just the client side of the flow.
The request, response, and push DTOs live in [`Shared`](./Shared) so the
server and client use the same protocol contract. The reusable client flow lives
in [`Client`](./Client); the server smoke path references it only to run the
all-in-one sample.

The server executable also supports separate roles:

```bash
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Server -- play
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Server -- api
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Client
```

Use `--api-url`, `--api-bind`, `--api-channel-endpoint`,
`--play-channel-endpoint`, `--play-endpoint`, and `--spot-endpoint` to override
the default local endpoints.
