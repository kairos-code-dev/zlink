# Bingo Session Gateway Sample

This sample implements the matching room Bingo flow from
`framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md`.

Directory layout:

- `Shared/` contains public sample DTOs, packet names, and timings.
- `Client/` contains the real stream connector client. The scenario creates four
  client connectors and connects all of them to the Session server stream
  endpoint.
- `Server/Api/` contains player authentication and matching API handlers.
- `Server/Play/` contains player actors, Entry Spot admission, room spots, timer
  draws, automatic marks, winner detection, and session-bound push.
- `Server/Session/` contains the stream Session server and actor relay handlers.
- `Server/Registry/` contains the embedded discovery registry host.
- `Server/Infrastructure/` contains registry-backed route and session binding
  stores shared by the server roles.
- `Server/Scenario/` starts the server roles and runs the client flow.

Run it with:

```bash
dotnet run --project "framework/languages/dotnet/samples/Bingo(session-gateway)/Server/Bingo.SessionGateway.Server.csproj"
```

The executable is self-checking. It fails if the four connector clients do not
authenticate as distinct actors, match into one room, set the first joiner as
host, reject early/non-host start requests, start by host request, draw numbers
by room timer, produce same-sequence winners, or deliver push notifications to
the bound client sessions.
