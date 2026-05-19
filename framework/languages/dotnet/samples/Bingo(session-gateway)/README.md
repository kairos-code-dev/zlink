# Bingo Session Gateway Sample

This sample implements the flow described by
`framework/languages/dotnet/doc/guide/samples/bingo-game-sample.ko.md`.

It keeps the sample small enough to inspect while preserving the documented
roles:

- `SessionServer` authenticates stream clients, binds them to player actors, and
  relays client requests to those actors.
- `ApiServer` owns player authentication and match allocation requests.
- `PlayServer` owns player actors, `BingoEntrySpot`, and `BingoRoomSpot`.
- `BingoEntrySpot` accepts actors before they join a room.
- `BingoRoomSpot` owns host selection, seats, cards, timer draws, automatic
  marks, winner detection, and server push notifications.

Run it with:

```bash
dotnet run --project "framework/languages/dotnet/samples/Bingo(session-gateway)/Bingo.SessionGateway.csproj"
```

The executable is self-checking. It throws if the documented completion criteria
stop holding: four distinct authenticated actors, one room assignment through
`MatchBingoReq`, first actor as host, start rejection before four players,
non-host start rejection, timer-driven server marks, same-sequence winners, and
push notifications delivered through the bound actor session.
