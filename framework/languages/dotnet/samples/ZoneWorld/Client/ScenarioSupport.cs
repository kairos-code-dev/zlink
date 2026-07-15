using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using ZoneWorld.Server.Configuration;
using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Client;

public sealed class ScenarioFailure(string message) : Exception(message);

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

            state = await MoveAndWaitForPositionAsync(nextX, nextY, cancellationToken);
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

    /// <summary>
    /// Arms the push waiter before sending the command. Immediate dispatch can deliver a local
    /// move result before code that sends first has registered its waiter.
    /// </summary>
    public async ValueTask<T> MoveAndWaitAsync<T>(
        int x,
        int y,
        Func<T, bool> predicate,
        CancellationToken cancellationToken)
        where T : class
    {
        var pending = WaitAsync(predicate, cancellationToken);
        Move(x, y);
        return await pending;
    }

    public ValueTask<ZoneStateNotify> MoveAndWaitForPositionAsync(
        int x,
        int y,
        CancellationToken cancellationToken) =>
        MoveAndWaitAsync<ZoneStateNotify>(
            x,
            y,
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
    /// Switches a node's maintenance mode and waits until the target's periodic status report
    /// reaches Ops. The request reply proves that the target applied the command; the status
    /// push provides an observable convergence boundary without a timing delay.
    /// </summary>
    public async ValueTask<SetMaintenanceRes> SetMaintenanceAsync(
        string nodeId,
        bool enabled,
        CancellationToken cancellationToken)
    {
        var observed = connector.WaitFor<NodeStatusNotify>()
            .Where(received =>
                received.Payload.NodeId == nodeId &&
                received.Payload.Maintenance == enabled)
            .Timeout(TimeSpan.FromSeconds(10))
            .Async(cancellationToken);
        var applied = await connector
            .Request(new SetMaintenanceReq(nodeId, enabled))
            .Async<SetMaintenanceRes>(cancellationToken);
        if (applied.Error is null) await observed;
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
            var applied = await SetMaintenanceAsync(nodeId, enabled: false, cancellationToken);
            if (applied.Error is null && applied.Enabled) throw new ScenarioFailure(
                $"Node '{nodeId}' did not leave maintenance.");
        }
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
    public static ClientOptions From(ZoneWorldClientSettings settings) =>
        new(settings.GatewayEndpoint, settings.OpsEndpoint);
}
