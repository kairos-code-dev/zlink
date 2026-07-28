// Verifies OBS-C5 Rollout behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC5RolloutScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        if (context.Options.C5Phase is "sequential" or "both")
            await RunSequentialAsync(context);
        if (context.Options.C5Phase is "simultaneous" or "both")
            await RunSimultaneousAsync(context);
        Console.WriteLine($"scenario OBS-C5 phase={context.Options.C5Phase} passed");
    }

    private static async Task RunSequentialAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var actorId = $"obs-c5-sequential-{suffix}";
        var roomRid = $"room-c5-sequential-{suffix}";
        await context.PlayA.Post("/rooms").Body(new CreateRoomReq(roomRid)).AsyncRaw();
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        await context.JoinRoomAsync(connector, actorId, roomRid);
        var lobby = await context.ReturnToEntrySpotAsync(
            connector, actorId, "obs-c5-room-complete");
        var sourceNode = lobby.NodeRid;
        _ = await context.PlayNodeIdAsync("play-a");
        _ = await context.PlayNodeIdAsync("play-b");
        var targetNode = context.OtherPlayNode(sourceNode);
        var source = context.Play(sourceNode);
        await context.PlayA.Post($"/rooms/{roomRid}/close").AsyncRaw();
        await source.Post("/relocate?deadlineMs=30000").AsyncRaw();
        await WaitActorAsync(
            context, actorId, targetNode, TimeSpan.FromSeconds(15));
        var status = await ScenarioContext.WaitForRelocationAsync(
            source, TimeSpan.FromSeconds(35));
        ZlinkStreamAssert.Ensure(status.Result == "Relocated",
            $"OBS-C5 sequential rollout returned {status.Result}/{status.Reason}.");
        var metrics = (await source.Get("/evidence")
            .Async<EvidenceSnapshot>()).Body.Metrics;
        ZlinkStreamAssert.Ensure(
            metrics.All(sample =>
                sample.Name != "zlink.host.shutdown.forced"),
            "OBS-C5 sequential relocation started forced Shutdown.");
        await connector.Close.Async();
    }

    private static async Task RunSimultaneousAsync(ScenarioContext context)
    {
        _ = await context.PlayNodeIdAsync("play-a");
        _ = await context.PlayNodeIdAsync("play-b");
        var suffix = Guid.NewGuid().ToString("N");
        var blockedRoomId = $"room-c5-blocked-{suffix}";
        var blockedRoom = (await context.PlayA.Post("/rooms")
            .Body(new CreateRoomReq(blockedRoomId))
            .Async<CreateRoomRes>()).Body;
        var blockedNode = blockedRoom.NodeRid;
        var blockedHost = context.Play(blockedNode);

        await blockedHost.Post("/operation-gate/arm")
            .Query("maximumWaitMs", "30000").AsyncRaw();
        var blockedOperation = blockedHost.Post("/operation/start")
            .Body(new PlayBoundedOperationReq(
                blockedRoomId,
                "obs-c5-block-target"))
            .Timeout(TimeSpan.FromSeconds(35))
            .Async<PlayBoundedOperationRes>().AsTask();
        await blockedHost.Post("/operation-gate/wait-started")
            .Query("timeoutMs", "5000").AsyncRaw();

        await blockedHost.Post("/relocate?deadlineMs=30000").AsyncRaw();
        await WaitPeerDrainingAsync(context, blockedNode, TimeSpan.FromSeconds(8));

        var actorId = $"obs-c5-zero-target-{suffix}";
        await using var connector = await context.ConnectAsync();
        var authenticated = await connector.Request(new AuthenticateReq(actorId))
            .Async<AuthenticateRes>();
        var sourceNode = authenticated.NodeRid;
        var sourceHost = context.Play(sourceNode);
        ZlinkStreamAssert.Ensure(sourceNode != blockedNode,
            "OBS-C5 placed new work on a node already marked for relocation.");

        var planned = (await sourceHost.Post("/relocate/direct")
            .Body(new RelocateHostReq(
                "planned-maintenance", null, 1000))
            .Async<RelocateHostRes>()).Body;
        var rolling = (await sourceHost.Post("/relocate/direct")
            .Body(new RelocateHostReq(
                "rolling-update", 99, 1000))
            .Async<RelocateHostRes>()).Body;
        var unchanged = (await sourceHost.Get("/evidence")
            .Query("actorId", actorId)
            .Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(
            unchanged.ActorRows.Any(row =>
                row.ActorId == actorId && row.NodeRid == sourceNode),
            "OBS-C5 zero-target preflight changed Actor authority.");
        ZlinkStreamAssert.Ensure(
            planned is
            {
                Outcome: "Blocked",
                Reason: "TargetUnavailable"
            }
            && rolling is
            {
                Outcome: "Blocked",
                Reason: "TargetUnavailable"
            },
            $"OBS-C5 zero-target modes returned planned="
            + $"{planned.Outcome}/{planned.Reason}, rolling="
            + $"{rolling.Outcome}/{rolling.Reason}.");
        var sourceStatus = (await sourceHost.Get("/runtime/status")
            .Async<RuntimeStatusRes>()).Body;
        ZlinkStreamAssert.Ensure(
            sourceStatus is
            {
                State: "Serving",
                IsReady: true,
                AcceptingWork: true
            },
            "OBS-C5 preflight blocker changed source readiness.");
        var action = await connector.Request(
                new GameActionReq("obs-c5-still-serving"))
            .Async<GameActionRes>();
        ZlinkStreamAssert.Ensure(
            action.NodeRid == sourceNode,
            "OBS-C5 handler admission did not remain on the source.");
        await blockedHost.Post("/operation-gate/release").AsyncRaw();
        var released = await blockedOperation;
        ZlinkStreamAssert.Ensure(released.Body.NodeRid == blockedNode,
            "OBS-C5 bounded handler did not complete on its observed owner.");
        ZlinkStreamAssert.Ensure(
            unchanged.Entries.All(entry =>
                !entry.Contains("spot-closing", StringComparison.Ordinal))
            && unchanged.Metrics.All(sample =>
                sample.Name != "zlink.relocation.started"),
            "OBS-C5 preflight blocker created relocation side effects.");
    }

    private static async Task<EvidenceSnapshot> WaitPeerDrainingAsync(
        ScenarioContext context,
        string nodeRid,
        TimeSpan timeout,
        string? actorId = null)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var request = context.PlayA.Get("/evidence");
            if (actorId is not null) request = request.Query("actorId", actorId);
            var snapshot = (await request.Async<EvidenceSnapshot>()).Body;
            if (snapshot.PeerRows.Any(row => row.NodeRid == nodeRid && row.Draining)
                && (actorId is null || snapshot.ActorRows.Any(row => row.ActorId == actorId)))
                return snapshot;
            await Task.Delay(100);
        }
        throw new TimeoutException($"OBS-C5 draining marker for '{nodeRid}' did not converge.");
    }

    private static async Task WaitActorAsync(
        ScenarioContext context, string actorId, string nodeRid, TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = (await context.PlayB.Get("/evidence").Query("actorId", actorId)
                .Async<EvidenceSnapshot>()).Body;
            if (snapshot.ActorRows.Any(row => row.ActorId == actorId && row.NodeRid == nodeRid)) return;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C5 sequential actor location did not converge.");
    }

}
