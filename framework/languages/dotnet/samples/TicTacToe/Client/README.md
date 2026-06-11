# Tic Tac Toe Client

This is the standalone sample client for `TicTacToe`.

Start the play and API roles first:

```bash
framework/languages/dotnet/samples/TicTacToe/run_sample.sh
```

On Windows PowerShell:

```powershell
.\framework\languages\dotnet\samples\TicTacToe\run_sample.ps1
```

The runner starts the play and API roles, waits for their endpoints, runs this
client, and stops the servers. To run the client against already running roles:

```bash
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Client
```

Options:

```bash
dotnet run --project framework/languages/dotnet/samples/TicTacToe/Client -- \
  --api-url http://127.0.0.1:18080 \
  --game-name tictactoe-game \
  --x-actor-id player-x \
  --o-actor-id player-o
```

Each actor id is sent as the sample authentication token. The API server
returns that value as `actorId`, and the play server uses it as the actor
`ActorId`. The sample client opens two STREAM connections, joins both actors to
one game, receives `PlayerJoinedNotify` and `GameStateNotify` push packets, then
plays a fixed five-move sequence where X wins. The game SPOT also owns a timer
that ends the game with `TurnTimedOut` when the current player takes too long.
