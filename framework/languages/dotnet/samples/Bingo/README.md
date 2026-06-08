# Bingo Sample

This sample implements the matching room Bingo flow from
`framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md`.

Directory layout:

- `Shared/` contains public sample DTOs, packet names, and timings.
- `Client/` contains the real stream connector client. The scenario creates two
  client connectors and connects all of them to the Session server stream
  endpoint.
- `Server/Api/` contains player authentication and matching API handlers.
- `Server/Play/` contains player actors, Entry Spot admission, room spots,
  submitted cards, client-driven number requests, automatic marks, winner
  detection, and session-bound push.
- `Server/Session/` contains the stream Session server and actor relay handlers.
- `Server/Registry/` contains the embedded discovery registry host.

Each top-level client/shared directory and each server role directory has its
own project file. The root `Bingo.csproj` is the aggregate build entry point for
IDEs and CLI builds. `Bingo.sln` keeps the same layout: `Shared`, `Client`, and
`Server/<role>`.

Build the whole sample with:

```bash
dotnet build "framework/languages/dotnet/samples/Bingo/Bingo.csproj"
```

Run it with:

```bash
framework/languages/dotnet/samples/Bingo/run_sample.sh
```

The script starts the Registry, Api, Play, and Session server projects as
separate processes, then runs the connector client. The client flow is
self-checking. It fails if the two connector clients do not authenticate as
distinct actors, match into one room, automatically start after the second
player submits a card, drive the game with number requests, produce the expected
winner, or deliver push notifications to the bound client sessions.
