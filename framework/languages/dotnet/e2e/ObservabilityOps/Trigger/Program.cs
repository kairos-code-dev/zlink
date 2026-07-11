using System.Diagnostics.Metrics;
using Bingo.Shared.Contracts;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.Codecs.Protobuf;

if (args.Length < 2)
    throw new ArgumentException(
        "Usage: ObservabilityOps.Trigger flow-action|error|metrics-reconnect <endpoint> | metrics-session <endpoint> <release-file> | hold-play <endpoint-a> <endpoint-b> | hold-session <endpoint>");

switch (args[0])
{
    case "error":
        await TriggerErrorAsync(args[1]);
        break;
    case "flow-action":
        await TriggerFlowActionAsync(args[1]);
        break;
    case "metrics-session" when args.Length == 3:
        await MeasureSessionsAsync(args[1], args[2]);
        break;
    case "metrics-reconnect":
        await MeasureReconnectAsync(args[1]);
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

static async Task MeasureReconnectAsync(string endpoint)
{
    long reconnects = 0;
    using var listener = new MeterListener
    {
        InstrumentPublished = static (instrument, owner) =>
        {
            if (instrument.Meter.Name == "zlink.framework"
                && instrument.Name == "zlink.stream.reconnects")
                owner.EnableMeasurementEvents(instrument);
        }
    };
    listener.SetMeasurementEventCallback<long>((_, value, _, _) =>
        Interlocked.Add(ref reconnects, value));
    listener.Start();

    await using var connector = Create(endpoint, persistentReconnect: true);
    var sawReconnecting = 0;
    var reconnected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
    connector.ConnectionStateChanged += (change, _) =>
    {
        if (change.Current == ZlinkStreamConnectionState.Reconnecting)
            Volatile.Write(ref sawReconnecting, 1);
        else if (change.Current == ZlinkStreamConnectionState.Connected
                 && Volatile.Read(ref sawReconnecting) != 0)
            reconnected.TrySetResult();
        return ValueTask.CompletedTask;
    };
    await connector.Connect.Async();
    Console.WriteLine("OBS-B1 reconnect=ready");
    await reconnected.Task.WaitAsync(TimeSpan.FromSeconds(30));
    if (Interlocked.Read(ref reconnects) <= 0)
        throw new InvalidOperationException("Connector reconnected without incrementing its reconnect counter.");
    Console.WriteLine($"OBS-B1 reconnect=completed attempts={Interlocked.Read(ref reconnects)}");
    await connector.Close.Async();
}

static async Task MeasureSessionsAsync(string endpoint, string releaseFile)
{
    var connectors = Enumerable.Range(0, 3).Select(_ => Create(endpoint)).ToArray();
    try
    {
        foreach (var connector in connectors) await connector.Connect.Async();
        Console.WriteLine("OBS-B1 connections=ready count=3");
        await WaitForFileAsync(releaseFile, TimeSpan.FromSeconds(20));
        foreach (var connector in connectors) await connector.Close.Async();
        Console.WriteLine("OBS-B1 connections=released count=3");
    }
    finally
    {
        foreach (var connector in connectors) await connector.DisposeAsync();
    }
}

static async Task WaitForFileAsync(string path, TimeSpan timeout)
{
    var fullPath = Path.GetFullPath(path);
    var deadline = DateTimeOffset.UtcNow + timeout;
    while (!File.Exists(fullPath))
    {
        if (DateTimeOffset.UtcNow >= deadline)
            throw new TimeoutException($"Release file '{fullPath}' was not created before {timeout}.");
        await Task.Delay(TimeSpan.FromMilliseconds(25));
    }
}

static async Task TriggerFlowActionAsync(string endpoint)
{
    await using var connector = Create(endpoint);
    await AuthenticateAsync(connector, BingoSamplePlayers.Player1);
    var match = await connector.Request(new MatchBingoReq { Mode = BingoSampleModes.TwoPlayer })
        .Async<MatchBingoRes>();
    await connector.Close.Async();
    Console.WriteLine($"OBS-A3 action=completed room={match.RoomId}");
}

static async Task TriggerErrorAsync(string endpoint)
{
    await using var connector = Create(endpoint);
    await AuthenticateAsync(connector, BingoSamplePlayers.Player2);
    try
    {
        _ = await connector
            .Request(new ZlinkStreamEncodedPayload(ZlinkStreamCodec.Raw, new byte[] { 0xff, 0x00 }))
            .PacketName("ObservabilityMissingPacket")
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        throw new InvalidOperationException("Missing packet request unexpectedly succeeded.");
    }
    catch (ZlinkStreamException error)
    {
        Console.WriteLine($"OBS-A2 error={error.Error.Code}");
    }
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

static IZlinkStreamConnector Create(string endpoint, bool persistentReconnect = false) =>
    ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
    {
        Endpoint = new Uri(endpoint),
        ConnectTimeout = TimeSpan.FromSeconds(5),
        RequestTimeout = TimeSpan.FromSeconds(30),
        Reconnect = persistentReconnect
            ? new ZlinkStreamReconnectOptions
            {
                Enabled = true,
                InitialDelay = TimeSpan.FromMilliseconds(200),
                MaxDelay = TimeSpan.FromMilliseconds(500),
                BackoffFactor = 2,
                MaxAttempts = null
            }
            : new ZlinkStreamReconnectOptions(),
        DispatchMode = ZlinkStreamDispatchMode.Immediate,
        PayloadCodec = ZLinkProtobufCodec.Default
    });
