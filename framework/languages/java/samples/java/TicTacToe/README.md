# TicTacToe

Direct STREAM, Spot, and channel-flow sample check.

The sample is split into standalone Spring role projects:

- `Client`: sends an HTTP `CreateGameHttpReq` to the API role, opens two STREAM
  connections to the Play role, authenticates both players, joins one game, and
  plays a fixed winning sequence. It also registers typed STREAM handlers for
  `GameStateNotify` and `PlayerJoinedNotify`.
- `Server`: starts one Spring Boot role per process. Use `play` for the Play
  role and `api` for the API role. The API role exposes the `/games` HTTP
  endpoint plus `AuthenticatePlayer` channel handler. The Play role owns the
  STREAM endpoint, actor runtime, entry Spot, and game Spot.
- `Shared`: holds the typed contracts used by the client, API role, Play role,
  and STREAM messages.

Run the standalone role sample check:

```bash
./run_sample.sh
```

Run the roles manually:

```bash
gradle :Server:run --args='play'
gradle :Server:run --args='api'
gradle :Client:run
```
