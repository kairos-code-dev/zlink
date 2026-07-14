using ZoneWorld.Shared.Contracts;

namespace ZoneWorld.Client;

/// <summary>
/// The self-check from §11. Every assertion here is observable on the wire, which is why
/// no browser is involved: a browser would only add rendering and timing between the
/// server's behaviour and the verdict.
/// </summary>
public static class Scenarios
{
    /// <summary>
    /// Scenarios the client drives end to end. Running "all" runs these.
    /// </summary>
    public static IReadOnlyDictionary<string, Func<ClientOptions, CancellationToken, ValueTask>> All =>
        new Dictionary<string, Func<ClientOptions, CancellationToken, ValueTask>>(StringComparer.OrdinalIgnoreCase)
        {
            ["ZW-A1"] = A1EnterAndMove,
            ["ZW-A2"] = A2RejectionOrder,
            ["ZW-A3"] = A3SameZonePlayers,
            ["ZW-A4"] = A4DiagonalCrossing,
            ["ZW-A5"] = A5SameZonePositionUpdate,
            ["ZW-B1"] = B1BorderSync,
            ["ZW-B2"] = B2CrossNodeTransfer,
            ["ZW-B3"] = B3IntraNodeZoneChange,
            ["ZW-C1"] = C1WatchNodes,
            ["ZW-C4"] = C4SpotEventReported,
            ["ZW-D1"] = D1AnnounceAllNodes,
            ["ZW-E1"] = E1TargetedMaintenance,
            ["ZW-E2"] = E2MaintainedNodeKeepsMoving,
            ["ZW-E3"] = E3LeavingMaintainedNode,
            ["ZW-E4"] = E4NodeDiagnostics,
            ["ZW-E6"] = E6MaintenanceBlocksEntry,
            ["ZW-F1"] = F1BotsPresent,
            ["ZW-F3"] = F3NoPushToBots,
            ["ZW-F4"] = F4BotReversesOnRejection
        };

    /// <summary>
    /// Scenarios that need the runner to stop or restart a node while they watch. They are
    /// addressed by id, never by "all": the runner has to disrupt the topology around them, so
    /// running them blind would just make them time out.
    /// </summary>
    public static IReadOnlyDictionary<string, Func<ClientOptions, CancellationToken, ValueTask>> RunnerDriven =>
        new Dictionary<string, Func<ClientOptions, CancellationToken, ValueTask>>(StringComparer.OrdinalIgnoreCase)
        {
            ["ZW-B4"] = B4BorderSnapshotExpiry,
            ["ZW-C2"] = C2NodeShutdown,
            ["ZW-C3"] = C3NodeDisconnected,
            ["ZW-E5-arm"] = E5Arm,
            ["ZW-E5"] = E5MaintenanceRestored
        };

    // --- Track A: entry and movement ----------------------------------------

    private static async ValueTask A1EnterAndMove(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("a1"), ct);
        var join = await player.JoinWorldAsync(ct);

        Assert.Equal(ZoneIds.NorthWest, join.ZoneId, "a new player spawns in zone-nw");
        Assert.Equal(NodeIds.West, join.NodeId, "zone-nw is hosted by zone-node-1");
        Assert.Equal(ZoneWorldSpec.SpawnX, join.X, "the spawn coordinate is fixed");
        Assert.Equal(ZoneWorldSpec.SpawnY, join.Y, "the spawn coordinate is fixed");

        player.Move(join.X + 3, join.Y + 2);
        var state = await player.WaitForPositionAsync(join.X + 3, join.Y + 2, ct);

        Assert.Equal(ZoneIds.NorthWest, state.ZoneId, "the move stayed inside zone-nw");
    }

    /// <summary>
    /// A move can break several rules at once, and every language must name the same one.
    /// This target is out of range *and* further than the step cap; the fixed order says
    /// OutOfRange wins (§2.2).
    /// </summary>
    private static async ValueTask A2RejectionOrder(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("a2"), ct);
        var join = await player.JoinWorldAsync(ct);

        player.Move(-40, join.Y);
        var rejected = await player.WaitAsync<MoveRejectedNotify>(_ => true, ct);

        Assert.Equal(MoveRejectReasons.OutOfRange, rejected.Reason, "OutOfRange is checked before TooFar");
        Assert.Equal(join.X, rejected.X, "a refused move leaves the coordinate untouched");
        Assert.Equal(join.Y, rejected.Y, "a refused move leaves the coordinate untouched");
    }

    private static async ValueTask A3SameZonePlayers(ClientOptions options, CancellationToken ct)
    {
        var firstId = Unique("a3-b");
        var secondId = Unique("a3-a");

        await using var first = await GameClient.ConnectAsync(options.GatewayEndpoint, firstId, ct);
        await first.JoinWorldAsync(ct);
        await using var second = await GameClient.ConnectAsync(options.GatewayEndpoint, secondId, ct);
        await second.JoinWorldAsync(ct);

        // Each client sees the other — the canonical says so of both, not of one (§11 ZW-A3).
        foreach (var (client, other) in new[] { (first, secondId), (second, firstId) })
        {
            var state = await client.WaitAsync<ZoneStateNotify>(
                notify => notify.Players.Any(p => p.PlayerId == firstId)
                          && notify.Players.Any(p => p.PlayerId == secondId),
                ct);

            Assert.True(
                state.Players.Any(p => p.PlayerId == other),
                "both clients are in the same zone, so each is in the other's Players");

            // The ordering rule covers the whole list, bots included: it is what makes every
            // language produce the same list from the same world.
            var ids = state.Players.Select(p => p.PlayerId).ToArray();
            Assert.Sequence(
                ids.OrderBy(id => id, StringComparer.Ordinal),
                ids,
                "Players is ordered by PlayerId as UTF-8 bytes");
        }
    }

    /// <summary>A step to (50,50) from (49,49) crosses both boundaries at once and would land
    /// in a zone that shares no edge with the current one, so it is refused (§2.2).</summary>
    private static async ValueTask A4DiagonalCrossing(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("a4"), ct);
        await player.JoinWorldAsync(ct);
        await player.WalkToAsync(49, 49, ct);

        player.Move(50, 50);
        var rejected = await player.WaitAsync<MoveRejectedNotify>(_ => true, ct);

        Assert.Equal(MoveRejectReasons.DiagonalCrossing, rejected.Reason, "a move may not cross both boundaries");
        Assert.Equal(49, rejected.X, "a refused move leaves the coordinate untouched");
        Assert.Equal(49, rejected.Y, "a refused move leaves the coordinate untouched");
    }

    private static async ValueTask A5SameZonePositionUpdate(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("a5"), ct);
        var join = await player.JoinWorldAsync(ct);

        player.Move(join.X + 4, join.Y);
        var state = await player.WaitForPositionAsync(join.X + 4, join.Y, ct);

        var me = player.Me(state);
        Assert.Equal(join.X + 4, me.X, "the zone spot's copy follows the actor's coordinate");
        Assert.Equal(ZoneIds.NorthWest, me.ZoneId, "the zone did not change");
    }

    // --- Track B: borders and transfer ---------------------------------------

    /// <summary>
    /// A player inside the border band is visible from the zone across that edge — and only
    /// that one. The diagonal zone shares no edge, so it never sees them (§4.1).
    /// </summary>
    private static async ValueTask B1BorderSync(ClientOptions options, CancellationToken ct)
    {
        var westId = Unique("b1-w");
        var eastId = Unique("b1-e");
        var diagonalId = Unique("b1-d");

        await using var west = await GameClient.ConnectAsync(options.GatewayEndpoint, westId, ct);
        await west.JoinWorldAsync(ct);
        await using var east = await GameClient.ConnectAsync(options.GatewayEndpoint, eastId, ct);
        await east.JoinWorldAsync(ct);
        await using var diagonal = await GameClient.ConnectAsync(options.GatewayEndpoint, diagonalId, ct);
        await diagonal.JoinWorldAsync(ct);

        // The eastern player crosses into zone-ne, which shares zone-nw's X edge. The diagonal
        // player goes to zone-se, which shares no edge with zone-nw at all. The western one
        // stands in zone-nw's band, close enough to be visible across an edge it shares.
        await east.WalkToAsync(55, 25, ct);
        await diagonal.WalkToAsync(55, 55, ct);
        await west.WalkToAsync(45, 45, ct);

        var seenFromEast = await east.WaitAsync<ZoneStateNotify>(
            notify => notify.ZoneId == ZoneIds.NorthEast
                      && notify.Players.Any(p => p.PlayerId == westId),
            ct);

        var neighbour = seenFromEast.Players.First(p => p.PlayerId == westId);
        Assert.Equal(ZoneIds.NorthWest, neighbour.ZoneId, "the neighbour is reported with its own zone");
        Assert.True(neighbour.X >= 40, "only players inside the band cross the border");

        // The negative control: zone-se shares no edge with zone-nw, so the same player must
        // never appear there — not once, over a run of ticks (§4.1). Only zone-se's own ticks
        // count; the walk across the map leaves stragglers from the zones it passed through.
        for (var tick = 0; tick < BorderObservationTicks; tick++)
        {
            var state = await diagonal.WaitAsync<ZoneStateNotify>(
                notify => notify.ZoneId == ZoneIds.SouthEast
                          && notify.Players.Any(p => p.PlayerId == diagonalId),
                ct);
            Assert.True(
                state.Players.All(p => p.PlayerId != westId),
                "a zone that shares no edge never sees the player across the diagonal");
        }
    }

    /// <summary>Long enough for a border snapshot to have arrived and expired twice over, so
    /// "never appears" is an observation rather than a coincidence of timing.</summary>
    private const int BorderObservationTicks = ZoneWorldSpec.BorderSnapshotExpiryTicks * 2;

    /// <summary>
    /// Crossing the X boundary moves the player onto the other node. The actor transfers,
    /// and the client's WebSocket stays up throughout — that is the whole point (ZW-B2).
    /// </summary>
    private static async ValueTask B2CrossNodeTransfer(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("b2"), ct);
        await player.JoinWorldAsync(ct);
        await player.WalkToAsync(48, 25, ct);

        player.Move(52, 25);
        var changed = await player.WaitAsync<ZoneChangedNotify>(_ => true, ct);

        Assert.Equal(ZoneIds.NorthEast, changed.ZoneId, "the X boundary leads into zone-ne");
        Assert.Equal(NodeIds.East, changed.NodeId, "zone-ne is hosted by zone-node-2");
        Assert.True(changed.Transferred, "a zone on another node means the actor transferred");

        // The same connection keeps working: the bound session followed the actor.
        player.Move(55, 25);
        var state = await player.WaitForPositionAsync(55, 25, ct);
        Assert.Equal(ZoneIds.NorthEast, state.ZoneId, "the client keeps playing on the new node");
    }

    /// <summary>The Y boundary stays inside one node, so no transfer happens (§2.6).</summary>
    private static async ValueTask B3IntraNodeZoneChange(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("b3"), ct);
        await player.JoinWorldAsync(ct);
        await player.WalkToAsync(25, 48, ct);

        player.Move(25, 52);
        var changed = await player.WaitAsync<ZoneChangedNotify>(_ => true, ct);

        Assert.Equal(ZoneIds.SouthWest, changed.ZoneId, "the Y boundary leads into zone-sw");
        Assert.Equal(NodeIds.West, changed.NodeId, "zone-sw is on the same node as zone-nw");
        Assert.True(!changed.Transferred, "a zone on the same node means no transfer");
    }

    // --- Track C: observing the nodes ----------------------------------------

    private static async ValueTask C1WatchNodes(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);

        var west = await ops.WaitAsync<NodeStatusNotify>(
            notify => notify.NodeId == NodeIds.West && notify.Registered,
            ct);
        Assert.True(west.Zones.Contains(ZoneIds.NorthWest), "zone-node-1 reports the zones it hosts");

        // Registration and connection are two different observations — the location runtime
        // reports one, the socket events the other — and the console has to show both (§8.1).
        var nodes = await ops.WatchNodesAsync(ct);
        foreach (var nodeId in new[] { NodeIds.West, NodeIds.East })
        {
            var node = nodes.Nodes.FirstOrDefault(n => n.NodeId == nodeId);
            Assert.True(node is not null, $"the console knows about {nodeId}");
            Assert.True(node!.Registered, $"{nodeId} is registered");
            Assert.True(node.Connected, $"{nodeId} is connected");
        }
    }

    /// <summary>
    /// A zone spot's timer handler fails. Ops cannot subscribe to a remote node's spot events
    /// — a spot event source only covers the SpotNode in the same process — so the node
    /// observes the failure locally and reports it (§8.1). The console sees it as an alert.
    /// </summary>
    private static async ValueTask C4SpotEventReported(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);

        // Arm the wait before asking to watch: the alert may already have happened, and the
        // reply to WatchNodesReq is what replays it.
        var waiting = ops.WaitAsync<NodeAlertNotify>(
            notify => notify.Kind == NodeAlertKinds.TimerHandlerFailed,
            ct);
        await ops.WatchNodesAsync(ct);
        var alert = await waiting;

        // The fault is injected into one node only (the runner gives zone-node-1 the failing
        // zone), so the alert has to name that node. An alert from anywhere else would mean the
        // report carries no identity, which is the whole point of routing it through the node.
        Assert.Equal(NodeAlertKinds.TimerHandlerFailed, alert.Kind, "the node reports its own spot event");
        Assert.Equal(NodeIds.West, alert.NodeId, "the alert names the node the fault was injected into");
    }

    // --- Track D: announcing to every node -----------------------------------

    /// <summary>
    /// One announcement leaves Ops without a node list and comes out of every node's fanout
    /// subscriber. What the client can judge is the part that reaches it: an announcement it
    /// receives is never a duplicate. Whether both nodes' subscribers and every zone spot got
    /// it is judged from the server logs by the runner, because no client can see another
    /// node's subscriber. Delivery to a player is best-effort, so a player that misses one is
    /// not a failure (§8.2, §11 ZW-D1).
    /// </summary>
    private static async ValueTask D1AnnounceAllNodes(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("d1"), ct);
        await player.JoinWorldAsync(ct);
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);

        var published = await ops.AnnounceAsync("server maintenance starts in 10 minutes", ct);
        Assert.True(published.AnnouncementId.Length > 0, "the publish is answered with an id");

        var received = await player.CollectAsync<WorldAnnounceNotify>(AnnouncementSettleTicks, ct);
        var ids = received.Select(notify => notify.AnnouncementId).ToArray();
        Assert.Sequence(
            ids.Distinct(StringComparer.Ordinal),
            ids,
            "a single publish reaches a player at most once — one node, one delivery");
    }

    /// <summary>How long to keep listening after a publish before deciding what arrived. Long
    /// enough for the fanout to have reached both nodes and their zone spots.</summary>
    private static readonly TimeSpan AnnouncementSettleTicks = TimeSpan.FromSeconds(3);

    // --- Track E: maintenance and diagnostics --------------------------------

    /// <summary>
    /// Maintenance names one node. The other node keeps taking players, which is what makes
    /// this a targeted call rather than a broadcast (§8.4).
    /// </summary>
    private static async ValueTask E1TargetedMaintenance(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        await ops.ResetMaintenanceAsync(ct);
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("e1"), ct);
        await player.JoinWorldAsync(ct);
        await player.WalkToAsync(48, 25, ct);

        var applied = await ops.SetMaintenanceAsync(NodeIds.East, enabled: true, ct);
        try
        {
            Assert.Equal(NodeIds.East, applied.NodeId, "maintenance applies to the named node");
            Assert.True(applied.Zones.Contains(ZoneIds.NorthEast) && applied.Zones.Contains(ZoneIds.SouthEast),
                "maintenance covers every zone of that node");

            // Entering zone-ne is refused; the coordinate does not move.
            player.Move(52, 25);
            var rejected = await player.WaitAsync<MoveRejectedNotify>(_ => true, ct);
            Assert.Equal(MoveRejectReasons.ZoneMaintenance, rejected.Reason, "the maintained node refuses arrivals");
            Assert.Equal(48, rejected.X, "a refused move leaves the coordinate untouched");

            // And so is entering zone-se. Maintenance names a *node*, so both of its zones have
            // to refuse — testing one would not tell the two apart (§11 ZW-E1).
            await player.WalkToAsync(48, 55, ct);
            player.Move(52, 55);
            var refusedSouth = await player.WaitAsync<MoveRejectedNotify>(_ => true, ct);
            Assert.Equal(MoveRejectReasons.ZoneMaintenance, refusedSouth.Reason,
                "the maintained node's other zone refuses arrivals too");
            Assert.Equal(48, refusedSouth.X, "a refused move leaves the coordinate untouched");

            // zone-node-1 is untouched: a move inside it still works.
            player.Move(45, 55);
            await player.WaitForPositionAsync(45, 55, ct);
        }
        finally
        {
            await ops.SetMaintenanceAsync(NodeIds.East, enabled: false, ct);
        }
    }

    /// <summary>Maintenance stops arrivals, not the players already there (§2.3).</summary>
    private static async ValueTask E2MaintainedNodeKeepsMoving(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        await ops.ResetMaintenanceAsync(ct);
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("e2"), ct);
        await player.JoinWorldAsync(ct);

        await ops.SetMaintenanceAsync(NodeIds.West, enabled: true, ct);
        try
        {
            // Moving inside the maintained node's zone.
            player.Move(30, 30);
            await player.WaitForPositionAsync(30, 30, ct);

            // And across a zone boundary inside the same node.
            await player.WalkToAsync(30, 48, ct);
            player.Move(30, 52);
            var changed = await player.WaitAsync<ZoneChangedNotify>(_ => true, ct);
            Assert.Equal(ZoneIds.SouthWest, changed.ZoneId, "an intra-node zone change is allowed under maintenance");
            Assert.True(!changed.Transferred, "no node was crossed");
        }
        finally
        {
            await ops.SetMaintenanceAsync(NodeIds.West, enabled: false, ct);
        }
    }

    /// <summary>Leaving a maintained node for a healthy one is allowed (§2.3).</summary>
    private static async ValueTask E3LeavingMaintainedNode(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        await ops.ResetMaintenanceAsync(ct);
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("e3"), ct);
        await player.JoinWorldAsync(ct);
        await player.WalkToAsync(48, 25, ct);

        await ops.SetMaintenanceAsync(NodeIds.West, enabled: true, ct);
        try
        {
            player.Move(52, 25);
            var changed = await player.WaitAsync<ZoneChangedNotify>(_ => true, ct);
            Assert.Equal(NodeIds.East, changed.NodeId, "leaving a maintained node for a healthy one is allowed");
            Assert.True(changed.Transferred, "the actor still transferred");
        }
        finally
        {
            await ops.SetMaintenanceAsync(NodeIds.West, enabled: false, ct);
        }
    }

    private static async ValueTask E4NodeDiagnostics(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);

        var diagnostics = await ops.DiagnoseAsync(NodeIds.West, ct);

        Assert.Equal(NodeIds.West, diagnostics.NodeId, "diagnostics come back from the node that was named");
        // The canonical answer is exactly these two zones — an extra one would mean the node is
        // hosting something it should not (§11 ZW-E4).
        Assert.Sequence(
            new[] { ZoneIds.NorthWest, ZoneIds.SouthWest },
            diagnostics.Zones.OrderBy(zone => zone, StringComparer.Ordinal),
            "zone-node-1 hosts zone-nw and zone-sw, and nothing else");
        Assert.True(diagnostics.PlayerCount >= 0, "the node reports how many players it holds");
    }

    /// <summary>A brand-new entry into a maintained node is refused (§2.3).</summary>
    private static async ValueTask E6MaintenanceBlocksEntry(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        await ops.ResetMaintenanceAsync(ct);
        await ops.SetMaintenanceAsync(NodeIds.West, enabled: true, ct);
        try
        {
            await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("e6"), ct);
            var join = await player.JoinWorldAsync(ct);
            Assert.Equal(MoveRejectReasons.ZoneMaintenance, join.Error, "the spawn node refuses a new entry");
        }
        finally
        {
            await ops.SetMaintenanceAsync(NodeIds.West, enabled: false, ct);
        }
    }

    /// <summary>
    /// A node that goes away is not there to answer a request, so the console learns about it
    /// from the location runtime rather than by polling (§8.1). The runner stops zone-node-2
    /// while this scenario is watching.
    /// </summary>
    private static async ValueTask C2NodeShutdown(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        var nodes = await ops.WatchNodesAsync(ct);

        // The node has to be registered before its going away means anything. "Not registered"
        // is also the state of a node the console has never heard of, and waiting for that
        // would pass before the runner had done anything.
        Assert.True(
            nodes.Nodes.Any(n => n.NodeId == NodeIds.East && n.Registered),
            "zone-node-2 is registered before the runner stops it");

        var gone = await ops.WaitAsync<NodeStatusNotify>(
            notify => notify.NodeId == NodeIds.East && !notify.Registered,
            ct);

        Assert.True(!gone.Registered, "a stopped node stops being registered");
    }

    /// <summary>
    /// The node's connection to Ops drops. This is a socket event, not a location one: the node
    /// may still be registered while its link is gone (§8.1). The runner stops zone-node-2 while
    /// this scenario is watching.
    /// </summary>
    private static async ValueTask C3NodeDisconnected(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        var nodes = await ops.WatchNodesAsync(ct);

        // As in ZW-C2: a link that was never up cannot drop, so the connected state has to be
        // established before the drop is observed — otherwise the default state passes the test.
        Assert.True(
            nodes.Nodes.Any(n => n.NodeId == NodeIds.East && n.Connected),
            "zone-node-2's link is up before the runner stops it");

        var dropped = await ops.WaitAsync<NodeStatusNotify>(
            notify => notify.NodeId == NodeIds.East && !notify.Connected,
            ct);

        Assert.True(!dropped.Connected, "a node whose link drops is reported as disconnected");
    }

    /// <summary>
    /// The adjacent zone's node stops. Its border snapshots stop arriving, and after three
    /// ticks the players it was reporting are dropped rather than left frozen on screen (§2.4).
    /// The runner stops zone-node-2 while this scenario is watching.
    /// </summary>
    private static async ValueTask B4BorderSnapshotExpiry(ClientOptions options, CancellationToken ct)
    {
        var westId = Unique("b4-w");
        var eastId = Unique("b4-e");

        await using var west = await GameClient.ConnectAsync(options.GatewayEndpoint, westId, ct);
        await west.JoinWorldAsync(ct);
        await using var east = await GameClient.ConnectAsync(options.GatewayEndpoint, eastId, ct);
        await east.JoinWorldAsync(ct);

        // The eastern player stands in zone-ne's band, so the western one can see it.
        await east.WalkToAsync(52, 25, ct);
        await west.WalkToAsync(45, 25, ct);
        await west.WaitAsync<ZoneStateNotify>(
            notify => notify.Players.Any(p => p.PlayerId == eastId),
            ct);

        // zone-node-2 goes away — the runner takes it away, and it waits long enough first for
        // the cross-node walk above to have finished. The wait here has to outlast that, or it
        // gives up before the thing it is watching for has been arranged.
        var expired = await west.WaitAsync<ZoneStateNotify>(
            notify => notify.ZoneId == ZoneIds.NorthWest
                      && notify.Players.All(p => p.PlayerId != eastId),
            TimeSpan.FromSeconds(60),
            ct);

        Assert.True(
            expired.Players.All(p => p.PlayerId != eastId),
            "a stopped node's players expire out of the neighbour's view");

        // Expiry drops the adjacent zone's snapshot whole, so *every* player it was reporting
        // goes at once — a snapshot is replaced, never merged (§2.4). The neighbour's own
        // players are untouched.
        Assert.True(
            expired.Players.All(p => p.ZoneId == ZoneIds.NorthWest),
            "the whole snapshot expires, not the one player the scenario was watching");
    }

    /// <summary>Puts zone-node-2 into maintenance so the runner can restart it (§8.4, ZW-E5).</summary>
    private static async ValueTask E5Arm(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        var applied = await ops.SetMaintenanceAsync(NodeIds.East, enabled: true, ct);
        Assert.True(applied.Error is null, "the node accepted maintenance before the restart");
    }

    /// <summary>
    /// Maintenance is desired state, not a message: the node reads it back from the store when
    /// it starts, so a restart does not quietly reopen a node the operator closed (§8.4).
    /// The runner restarts zone-node-2 between E5-arm and this scenario.
    /// </summary>
    private static async ValueTask E5MaintenanceRestored(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        try
        {
            var diagnostics = await ops.DiagnoseAsync(NodeIds.East, ct);
            Assert.True(diagnostics.Maintenance, "the restarted node came up still under maintenance");
        }
        finally
        {
            await ops.SetMaintenanceAsync(NodeIds.East, enabled: false, ct);
        }
    }

    // --- Track F: bots --------------------------------------------------------

    /// <summary>
    /// The world runs bots with no client attached, and they move on their own (§2.7). A client
    /// sees the bots of its own zone plus whichever are standing in an adjacent zone's border
    /// band — never the whole population of eight, because a client only ever receives one
    /// zone's view. The count of eight is judged by the runner from the server logs; what is
    /// asserted here is what a client can actually see.
    /// </summary>
    private static async ValueTask F1BotsPresent(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("f1"), ct);
        await player.JoinWorldAsync(ct);

        var first = await player.WaitAsync<ZoneStateNotify>(
            notify => notify.Players.Count(p => p.IsBot) >= ZoneWorldSpec.BotsPerZone,
            ct);
        var bots = first.Players.Where(p => p.IsBot).ToDictionary(p => p.PlayerId, StringComparer.Ordinal);
        Assert.True(
            bots.Count >= ZoneWorldSpec.BotsPerZone,
            "the spawn zone's own bots are in the world with no client attached");

        // Both of the zone's bots move: one patrols X, the other Y, and each has to be walking.
        var stillStanding = new HashSet<string>(bots.Keys, StringComparer.Ordinal);
        while (stillStanding.Count > 0)
        {
            var state = await player.NextTickAsync(ct);
            foreach (var bot in state.Players.Where(p => p.IsBot))
            {
                if (!bots.TryGetValue(bot.PlayerId, out var before)) continue;
                if (bot.X != before.X || bot.Y != before.Y) stillStanding.Remove(bot.PlayerId);
            }
        }
    }

    /// <summary>
    /// A bot has no bound session, so nothing is ever pushed to it. What the canonical asks for
    /// is the *absence* of a push attempt (§11 ZW-F3), and no client can observe an absence on
    /// another actor — so the runner judges it from the server logs, where a push to an unbound
    /// actor would leave an error. This scenario supplies the traffic that would provoke one:
    /// it puts bots in the world alongside a human, publishes an announcement the zone spots
    /// have to deliver, and drives a tick loop, all of which walk the push path.
    /// </summary>
    private static async ValueTask F3NoPushToBots(ClientOptions options, CancellationToken ct)
    {
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("f3"), ct);
        await player.JoinWorldAsync(ct);
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);

        await ops.AnnounceAsync("bots receive nothing", ct);

        // A rejected move is the other push (§2.2), and a bot must not be sent one either.
        player.Move(-40, player.Position.Y);
        await player.WaitAsync<MoveRejectedNotify>(_ => true, ct);

        var state = await player.NextTickAsync(ct);
        Assert.True(state.Players.Any(p => p.IsBot), "the bots are in the world alongside the human");
    }

    /// <summary>
    /// A rejected move turns a bot around (§2.7). The rejection is arranged rather than waited
    /// for: putting the eastern node under maintenance refuses the X-patrolling bot at the
    /// boundary, and it walks back the way it came.
    /// </summary>
    private static async ValueTask F4BotReversesOnRejection(ClientOptions options, CancellationToken ct)
    {
        await using var ops = await OpsClient.ConnectAsync(options.OpsEndpoint, ct);
        await ops.ResetMaintenanceAsync(ct);
        await using var player = await GameClient.ConnectAsync(options.GatewayEndpoint, Unique("f4"), ct);
        await player.JoinWorldAsync(ct);

        await ops.SetMaintenanceAsync(NodeIds.East, enabled: true, ct);
        try
        {
            // The bot to watch is an X patroller standing in zone-nw within one step of the
            // boundary: its next step lands on the maintained node, so the refusal it is about
            // to get is the one this scenario is about. Naming a bot up front would not work —
            // the bots wander across zones, so which one is here depends on when this runs.
            var atBoundary = await player.WaitAsync<ZoneStateNotify>(
                notify => AboutToCross(notify) is not null,
                ct);
            var botId = AboutToCross(atBoundary)!.PlayerId;
            var peak = BotX(atBoundary, botId)!.Value;

            // It is refused at the boundary and walks back the way it came.
            var reversed = await player.WaitAsync<ZoneStateNotify>(
                notify =>
                {
                    var x = BotX(notify, botId);
                    if (x is null) return false;
                    if (x > peak) peak = x.Value;
                    return x < peak;
                },
                ct);

            Assert.True(
                BotX(reversed, botId) < peak,
                "a bot refused entry to a node under maintenance turns around");
        }
        finally
        {
            await ops.SetMaintenanceAsync(NodeIds.East, enabled: false, ct);
        }
    }

    private static int? BotX(ZoneStateNotify state, string botId) =>
        state.Players.FirstOrDefault(p => p.PlayerId == botId)?.X;

    /// <summary>
    /// An X-patrolling bot (its id ends in "-x", §2.7) that is in zone-nw and close enough to
    /// the boundary that its next step crosses it. That is the bot whose next move the
    /// maintained node has to refuse.
    /// </summary>
    private static PlayerView? AboutToCross(ZoneStateNotify state) =>
        state.Players.FirstOrDefault(p =>
            p.IsBot
            && p.PlayerId.EndsWith("-x", StringComparison.Ordinal)
            && p.ZoneId == ZoneIds.NorthWest
            && p.X + ZoneWorldSpec.BotStep >= ZoneWorldSpec.ZoneSplit);

    private static string Unique(string prefix) =>
        $"{prefix}-{Guid.NewGuid().ToString("n")[..6]}";
}
