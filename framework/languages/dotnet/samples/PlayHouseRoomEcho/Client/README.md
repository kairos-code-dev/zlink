# PlayHouse Room Echo Client

This is the standalone sample client for `PlayHouseRoomEcho`.

Start the play and API roles first:

```bash
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Server -- play
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Server -- api
```

Then run the client:

```bash
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Client
```

Options:

```bash
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Client -- \
  --api-url http://127.0.0.1:18080 \
  --room-name echo-room \
  --player-id player-1 \
  --message o
```
