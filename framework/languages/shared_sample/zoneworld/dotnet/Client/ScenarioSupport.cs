using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Client;

public sealed class ScenarioFailure(string message) : Exception(message);

public static class Assert
{
    public static void True(bool condition, string because)
    {
        if (!condition) throw new ScenarioFailure(because);
    }

    public static void Equal<T>(T expected, T actual, string because)
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
            throw new ScenarioFailure($"{because} (expected={expected}, actual={actual})");
    }

    /// <summary>Order and membership both, for the places the canonical names an exact list
    /// rather than "contains".</summary>
    public static void Sequence<T>(IEnumerable<T> expected, IEnumerable<T> actual, string because)
    {
        var left = expected.ToArray();
        var right = actual.ToArray();
        if (left.SequenceEqual(right)) return;

        throw new ScenarioFailure(
            $"{because} (expected=[{string.Join(", ", left)}], actual=[{string.Join(", ", right)}])");
    }
}

/// <summary>
/// One connected client. The scenarios speak in these terms — join, move, wait for a tick —
/// so the assertions read as the scenario table does (§11) rather than as connector calls.
/// </summary>
public sealed class GameClient(IZlinkStreamConnector connector, string playerId) : IAsyncDisposable
{
    public string PlayerId { get; } = playerId;

    public JoinWorldRes? Join { get; private set; }

    /// <summary>Where the walk starts. It follows the moves this client made, so a scenario can
    /// walk somewhere, act, and walk on from there.</summary>
    public (int X, int Y) Position { get; private set; }

    public static async ValueTask<GameClient> ConnectAsync(
        string endpoint,
        string playerId,
        CancellationToken cancellationToken)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(10),
            RequestTimeout = TimeSpan.FromSeconds(10),
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        await connector.Connect.Async(cancellationToken);
        return new GameClient(connector, playerId);
    }

    public async ValueTask<JoinWorldRes> JoinWorldAsync(CancellationToken cancellationToken)
    {
        Join = await connector.Request(new JoinWorldReq(PlayerId))
            .Async<JoinWorldRes>(cancellationToken);
        Position = (Join.X, Join.Y);
        return Join;
    }

    public void Move(int x, int y) => connector.Send(new MoveMsg(x, y)).Submit();

    /// <summary>Moves toward a target one legal step at a time. The step cap is part of the
    /// contract (§2.2), so a scenario cannot jump across the map in one message.</summary>
    public async ValueTask<ZoneStateNotify> WalkToAsync(
        int targetX,
        int targetY,
        CancellationToken cancellationToken)
    {
        if (Join is null) throw new ScenarioFailure("The client must join the world first.");
        var (x, y) = Position;
        ZoneStateNotify? state = null;

        while (x != targetX || y != targetY)
        {
            var nextX = x + Math.Clamp(targetX - x, -ZoneWorldSpec.MaxStepPerAxis, ZoneWorldSpec.MaxStepPerAxis);
            var nextY = y + Math.Clamp(targetY - y, -ZoneWorldSpec.MaxStepPerAxis, ZoneWorldSpec.MaxStepPerAxis);

            // A move may not cross the X and Y boundary at once (§2.2), so a diagonal walk
            // settles one axis before touching the other.
            if (CrossesBoth(x, y, nextX, nextY)) nextY = y;

            Move(nextX, nextY);
            state = await WaitForPositionAsync(nextX, nextY, cancellationToken);
            (x, y) = (nextX, nextY);
            Position = (x, y);
        }

        return state ?? await NextTickAsync(cancellationToken);
    }

    public ValueTask<ZoneStateNotify> NextTickAsync(CancellationToken cancellationToken) =>
        WaitAsync<ZoneStateNotify>(notify => notify.Players.Any(p => p.PlayerId == PlayerId), cancellationToken);

    public ValueTask<ZoneStateNotify> WaitForPositionAsync(int x, int y, CancellationToken cancellationToken) =>
        WaitAsync<ZoneStateNotify>(
            notify => notify.Players.Any(p => p.PlayerId == PlayerId && p.X == x && p.Y == y),
            cancellationToken);

    public ValueTask<T> WaitAsync<T>(Func<T, bool> predicate, CancellationToken cancellationToken)
        where T : class =>
        WaitAsync(predicate, TimeSpan.FromSeconds(15), cancellationToken);

    public async ValueTask<T> WaitAsync<T>(
        Func<T, bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
        where T : class
    {
        var message = await connector.WaitFor<T>()
            .Where(received => predicate(received.Payload))
            .Timeout(timeout)
            .Async(cancellationToken);
        return message.Payload;
    }

    /// <summary>
    /// Everything of one kind that arrives within a window. Used where the canonical asserts a
    /// property of what arrives rather than that something arrives — best-effort delivery means
    /// an empty result is a valid observation, not a timeout (§8.2).
    /// </summary>
    public async ValueTask<IReadOnlyList<T>> CollectAsync<T>(
        TimeSpan window,
        CancellationToken cancellationToken)
        where T : class
    {
        var collected = new List<T>();
        var deadline = DateTime.UtcNow + window;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                collected.Add(await WaitAsync<T>(_ => true, deadline - DateTime.UtcNow, cancellationToken));
            }
            catch (Exception) when (!cancellationToken.IsCancellationRequested)
            {
                break;
            }
        }

        return collected;
    }

    public PlayerView Me(ZoneStateNotify state) =>
        state.Players.FirstOrDefault(p => p.PlayerId == PlayerId)
        ?? throw new ScenarioFailure($"Player '{PlayerId}' is missing from the zone state.");

    private static bool CrossesBoth(int fromX, int fromY, int toX, int toY)
    {
        var split = ZoneWorldSpec.ZoneSplit;
        return (fromX < split) != (toX < split) && (fromY < split) != (toY < split);
    }

    public ValueTask DisposeAsync() => connector.DisposeAsync();
}

/// <summary>The operator console's end of the world.</summary>
public sealed class OpsClient(IZlinkStreamConnector connector) : IAsyncDisposable
{
    public static async ValueTask<OpsClient> ConnectAsync(string endpoint, CancellationToken cancellationToken)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(10),
            RequestTimeout = TimeSpan.FromSeconds(10),
            DispatchMode = ZlinkStreamDispatchMode.Immediate
        });
        await connector.Connect.Async(cancellationToken);
        return new OpsClient(connector);
    }

    public ValueTask<WatchNodesRes> WatchNodesAsync(CancellationToken cancellationToken) =>
        connector.Request(new WatchNodesReq()).Async<WatchNodesRes>(cancellationToken);

    public ValueTask<AnnounceWorldRes> AnnounceAsync(string text, CancellationToken cancellationToken) =>
        connector.Request(new AnnounceWorldReq(text)).Async<AnnounceWorldRes>(cancellationToken);

    /// <summary>
    /// Switches a node's maintenance mode and waits for the other nodes to have seen it.
    /// The reply says the *target* node applied the change; the other nodes learn it from the
    /// fanout, which is asynchronous (§2.3). A move judged before that fanout lands reads a
    /// stale cache, so a scenario that changes maintenance and immediately moves has to let
    /// the change settle first.
    /// </summary>
    public async ValueTask<SetMaintenanceRes> SetMaintenanceAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken)
    {
        var applied = await connector
            .Request(new SetMaintenanceReq(nodeId, enabled))
            .Async<SetMaintenanceRes>(cancellationToken);

        await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        return applied;
    }

    public ValueTask<NodeDiagnosticsRes> DiagnoseAsync(string nodeId, CancellationToken cancellationToken) =>
        connector.Request(new NodeDiagnosticsReq(nodeId)).Async<NodeDiagnosticsRes>(cancellationToken);

    /// <summary>
    /// Brings every node out of maintenance and waits until each one confirms it. The scenarios
    /// share one topology, so a scenario that reads maintenance has to start from a state it
    /// knows rather than from whatever the previous one left behind.
    /// </summary>
    public async ValueTask ResetMaintenanceAsync(CancellationToken cancellationToken)
    {
        foreach (var nodeId in new[] { NodeIds.West, NodeIds.East })
        {
            var applied = await connector
                .Request(new SetMaintenanceReq(nodeId, false))
                .Async<SetMaintenanceRes>(cancellationToken);

            if (applied.Error is null && applied.Enabled) throw new ScenarioFailure(
                $"Node '{nodeId}' did not leave maintenance.");
        }

        await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
    }

    public async ValueTask<T> WaitAsync<T>(Func<T, bool> predicate, CancellationToken cancellationToken)
        where T : class
    {
        var message = await connector.WaitFor<T>()
            .Where(received => predicate(received.Payload))
            .Timeout(TimeSpan.FromSeconds(40))
            .Async(cancellationToken);
        return message.Payload;
    }

    public ValueTask DisposeAsync() => connector.DisposeAsync();
}

public sealed record ClientOptions(string GatewayEndpoint, string OpsEndpoint)
{
    public static ClientOptions Create() => new(
        ZoneWorldSettings.Text("ZONEWORLD_GATEWAY_STREAM", "ws://127.0.0.1:48080"),
        ZoneWorldSettings.Text("ZONEWORLD_OPS_STREAM", "ws://127.0.0.1:48090"));
}
