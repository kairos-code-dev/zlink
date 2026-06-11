# TicTacToe Kotlin

Kotlin version of the TicTacToe framework sample.

The Kotlin TicTacToe client and server roles use MessagePack payloads for STREAM
messages.
The client verification flow lives in `TicTacToeClientScenario`.

The sample is split into standalone Spring role projects:

- `Client`: sends an HTTP `CreateGameHttpReq` to the API role, opens two STREAM
  connections to the Play role, authenticates both players, joins one game, and
  plays a fixed winning sequence. It also registers typed STREAM handlers for
  `GameStateNotify` and `PlayerJoinedNotify`.
- `Server`: starts one Spring Boot role per process. Use `play` or `api` to run
  a role. The API role exposes the `/games` HTTP endpoint plus
  `AuthenticatePlayer` channel handler. The Play role owns the STREAM endpoint,
  actor runtime, entry Spot, and game Spot.
- `Shared`: holds the message contracts used by the client, API role, Play
  role, and STREAM messages.

Run the standalone role sample check:

```bash
./run_sample.sh
```

On Windows:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File .\run_sample.ps1
```

Run the roles manually:

```bash
gradle :Server:run --args='play --config ./application.properties'
gradle :Server:run --args='api --config ./application.properties'
gradle :Client:run
```
