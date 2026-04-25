# PlayHouse Room Echo Sample

This sample maps the PlayHouse room flow onto `Zlink.Framework`:

1. the client calls the API server over HTTP,
2. the API server asks the play server to create a room over a ZLink channel,
3. the API server returns the play endpoint and room id,
4. the client connects to the play STREAM endpoint,
5. the client joins the room, then sends `o`,
6. the room actor echoes `o` back to the client.

The sample is grouped by its own solution:

```bash
dotnet build framework/languages/dotnet/samples/PlayHouseRoomEcho/PlayHouseRoomEcho.sln
```

Run the all-in-one server smoke path:

```bash
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Server
```

The standalone client lives in [`Client`](./Client/). Use it when you want to
read or run just the client side of the flow.

The server executable also supports separate roles:

```bash
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Server -- play
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Server -- api
dotnet run --project framework/languages/dotnet/samples/PlayHouseRoomEcho/Client
```

Use `--api-url`, `--api-bind`, `--play-endpoint`, `--control-endpoint`, and
`--spot-endpoint` to override the default local endpoints.
