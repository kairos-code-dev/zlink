// Verifies OBS-C2 Actor Handoff behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC2ActorHandoffScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var actorId = $"obs-c2-{suffix}";
        var roomRid = $"room-c2-{suffix}";
        await context.PlayA.Post("/rooms").Body(new CreateRoomReq(roomRid)).AsyncRaw();
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        await context.JoinRoomAsync(connector, actorId, roomRid);
        var requests = Enumerable.Range(0, 3)
            .Select(index => connector.Request(new GameActionReq($"obs-c2-pending-{index}", 100))
                .Async<GameActionRes>().AsTask()).ToArray();
        var replies = await Task.WhenAll(requests);
        ZlinkStreamAssert.Ensure(replies.All(reply => reply.ActorId == actorId),
            "OBS-C2 a request lost its original reply during handoff.");
        var lobby = await context.ReturnToEntrySpotAsync(
            connector, actorId, "obs-c2-room-complete");
        ZlinkStreamAssert.Ensure(lobby.SpotId is null,
            "OBS-C2 actor did not leave the completed room.");
        var sourceNode = lobby.NodeRid;
        _ = await context.PlayNodeIdAsync("play-a");
        _ = await context.PlayNodeIdAsync("play-b");
        var targetNode = context.OtherPlayNode(sourceNode);
        var source = context.Play(sourceNode);
        await context.PlayA.Post($"/rooms/{roomRid}/close").AsyncRaw();
        await source.Post("/relocate?deadlineMs=30000").AsyncRaw();
        var location = await WaitActorLocationAsync(
            context, actorId, targetNode);
        ZlinkStreamAssert.Ensure(
            location.ActorRows.Any(row =>
                row.ActorId == actorId && row.NodeRid == targetNode),
            "OBS-C2 actor location did not commit to the eligible peer.");
        var result = await ScenarioContext.WaitForRelocationAsync(
            source, TimeSpan.FromSeconds(40));
        var metrics = (await source.Get("/evidence")
            .Async<EvidenceSnapshot>()).Body.Metrics;
        ZlinkStreamAssert.Ensure(result.Result == "Relocated",
            $"OBS-C2 serving target handoff failed: {result.Result}/{result.Reason}.");
        var moved = await connector.WaitFor<PlayerMovedNotify>()
            .Where(message => message.Payload.ActorId == actorId
                              && message.Payload.TargetNodeRid == targetNode)
            .Timeout(TimeSpan.FromSeconds(10))
            .Async();
        ZlinkStreamAssert.Ensure(moved.Payload.TargetNodeRid == targetNode,
            "OBS-C2 bound session did not receive the target handoff notification.");
        ZlinkStreamAssert.Ensure(metrics.Any(sample =>
                sample.Name == "zlink.relocation.completed"
                && sample.Tags.GetValueOrDefault("object_kind") == "actor"
                && sample.Tags.GetValueOrDefault("outcome") == "completed"
                && sample.Value >= 1),
            "OBS-C2 Actor relocation completion metric did not increase.");
        ZlinkStreamAssert.Ensure(metrics.Any(sample =>
                sample.Name == "zlink.mesh_node.requests.inflight"
                && sample.Tags.GetValueOrDefault("surface") == "actor"
                && sample.Count >= 1),
            "OBS-C2 in-flight Actor request was not observed.");
        await connector.Close.Async();
        Console.WriteLine("scenario OBS-C2 passed");
    }

    private static async Task<EvidenceSnapshot> WaitActorLocationAsync(
        ScenarioContext context,
        string actorId,
        string targetNode)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = (await context.PlayB.Get("/evidence").Query("actorId", actorId)
                .Async<EvidenceSnapshot>()).Body;
            if (snapshot.ActorRows.Any(row =>
                    row.ActorId == actorId && row.NodeRid == targetNode))
                return snapshot;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C2 actor handoff location did not converge.");
    }

}
