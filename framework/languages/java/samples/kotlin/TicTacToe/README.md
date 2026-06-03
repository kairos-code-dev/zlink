# TicTacToe Kotlin

Kotlin version of the TicTacToe framework sample.

The root project is a self-check entry point. The standalone role projects are:

- `Client`: sends an HTTP `CreateGameHttpReq` to the API role, opens two STREAM
  connections to the Play role, authenticates both players, joins one game, and
  plays a fixed winning sequence. It also registers typed STREAM handlers for
  `GameStateNotify` and `PlayerJoinedNotify`.
- `Server`: starts the API and Play roles as Spring Boot applications in one
  sample process. The API role exposes the `/games` HTTP endpoint plus
  `AuthenticatePlayer` channel handler. The Play role owns the STREAM endpoint,
  actor runtime, entry Spot, and game Spot.
- `Shared`: holds the typed contracts used by the client, API role, Play role,
  and STREAM messages.

Run the standalone role sample check:

```bash
./run_sample.sh
```

Run the aggregate self-check entry point:

```bash
gradle :run
```

Run the roles manually:

```bash
gradle :Server:run --args='server'
gradle :Client:run
```
