using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Codecs.Protobuf;

if (args.Length < 2)
    throw new ArgumentException(
        "Usage: ObservabilityOps.Trigger error <endpoint> | hold-play <endpoint-a> <endpoint-b> | hold-session <endpoint>");

switch (args[0])
{
    case "error":
        await TriggerErrorAsync(args[1]);
        break;
    case "hold-play" when args.Length == 3:
        await HoldPlayAsync(args[1], args[2]);
        break;
    case "hold-session":
        await HoldSessionAsync(args[1]);
        break;
    default:
        throw new ArgumentException("Unknown trigger mode.");
}

static async Task TriggerErrorAsync(string endpoint)
{
    await using var connector = Create(endpoint);
    await AuthenticateAsync(connector, BingoSamplePlayers.Player1);
    connector.Send(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 0xff, 0x00 }))
        .PacketName("ObservabilityMissingPacket")
        .Submit();
    await Task.Delay(250);
    await connector.Close.Async();
    Console.WriteLine("OBS-A2 trigger=completed");
}

static async Task HoldPlayAsync(string endpointA, string endpointB)
{
    await using var player1 = Create(endpointA);
    await using var player2 = Create(endpointB);
    await AuthenticateAsync(player1, BingoSamplePlayers.Player1);
    var first = await player1.Request(new MatchBingoReq { Mode = BingoSampleModes.TwoPlayer })
        .Async<MatchBingoRes>();
    await AuthenticateAsync(player2, BingoSamplePlayers.Player2);
    var second = await player2.Request(new MatchBingoReq { Mode = BingoSampleModes.TwoPlayer })
        .Async<MatchBingoRes>();
    if (first.RoomId != second.RoomId) throw new InvalidOperationException("Players did not join one room.");

    var player1Moved = player1.WaitFor<BingoActorEntrySpotNotify>()
        .Timeout(TimeSpan.FromSeconds(25))
        .Where(message => message.Payload.ActorId == BingoSamplePlayers.Player1)
        .Async().AsTask();
    var player2Moved = player2.WaitFor<BingoActorEntrySpotNotify>()
        .Timeout(TimeSpan.FromSeconds(25))
        .Where(message => message.Payload.ActorId == BingoSamplePlayers.Player2)
        .Async().AsTask();
    Console.WriteLine($"OBS-C1 hold=ready room={first.RoomId} owner={first.RoomOwnerNodeRid}");

    var moved = await Task.WhenAll(player1Moved, player2Moved)
        .WaitAsync(TimeSpan.FromSeconds(25));
    if (moved.Any(message => message.Payload.TargetNodeRid != "2202"))
        throw new InvalidOperationException("Actors did not move to play-b.");
    Console.WriteLine("OBS-C2 bound_push=continued target=2202");

    await player2.Close.Async();
    await player1.Close.Async();
    Console.WriteLine("OBS-C3 hold=released");
}

static async Task HoldSessionAsync(string endpoint)
{
    await using var connector = Create(endpoint);
    var disconnected = new TaskCompletionSource<ZlinkStreamCloseReason>(
        TaskCreationOptions.RunContinuationsAsynchronously);
    connector.Disconnected += (change, _) =>
    {
        disconnected.TrySetResult(change.CloseReason);
        return ValueTask.CompletedTask;
    };
    await AuthenticateAsync(connector, BingoSamplePlayers.Observer);
    Console.WriteLine("OBS-C4 session=ready");
    var reason = await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(15));
    Console.WriteLine($"OBS-C4 close_reason={reason}");
}

static async Task AuthenticateAsync(IZlinkStreamConnector connector, string player)
{
    await connector.Connect.Async();
    _ = await connector.Request(new AuthenticateReq { AccessToken = player })
        .Async<AuthenticateRes>();
}

static IZlinkStreamConnector Create(string endpoint) =>
    ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
    {
        Endpoint = new Uri(endpoint),
        ConnectTimeout = TimeSpan.FromSeconds(5),
        RequestTimeout = TimeSpan.FromSeconds(30),
        DispatchMode = ZlinkStreamDispatchMode.Immediate,
        PayloadCodec = ZLinkProtobufCodec.Default
    });
