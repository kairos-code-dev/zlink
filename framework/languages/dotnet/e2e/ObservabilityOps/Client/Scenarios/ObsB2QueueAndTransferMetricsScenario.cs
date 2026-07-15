using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsB2QueueAndTransferMetricsScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var actorId = $"obs-b2-{suffix}";
        var sourceRoom = $"room-b2-source-{suffix}";
        var targetRoom = $"room-b2-target-{suffix}";
        await context.PlayA.Post("/rooms").Body(new CreateRoomReq(sourceRoom)).AsyncRaw();
        await context.PlayB.Post("/rooms").Body(new CreateRoomReq(targetRoom)).AsyncRaw();
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        await connector.Request(new JoinRoomReq(sourceRoom)).Async<JoinRoomRes>();

        var queued = Enumerable.Range(0, 12)
            .Select(index => connector.Request(new GameActionReq($"obs-b2-load-{index}", 20))
                .Async<GameActionRes>().AsTask())
            .ToArray();
        await Task.WhenAll(queued);

        var moved = await connector.Request(new JoinRoomReq(targetRoom)).Async<JoinRoomRes>();
        ZlinkStreamAssert.Ensure(moved.NodeRid == "play-b", "OBS-B2 actor did not transfer to play-b.");
        var queueDepth = await WaitMetricAsync(context, "zlink.spot.queue.depth", 0,
            new Dictionary<string, string> { ["kind"] = "user" });
        var queueWait = await WaitMetricAsync(context, "zlink.spot.queue.wait.duration", 0,
            new Dictionary<string, string> { ["kind"] = "user" });
        var transfers = await WaitMetricAsync(context, "zlink.actor.transfers", 1);
        var transferDuration = await WaitMetricAsync(context, "zlink.actor.transfer.duration", 0);
        var pending = await WaitMetricAsync(context, "zlink.actor.transfer.pending_requests.count", 0);

        ZlinkStreamAssert.Ensure(queueDepth.Any(sample => sample.Count > 0),
            "OBS-B2 queue depth metric was not recorded.");
        ZlinkStreamAssert.Ensure(queueWait.Any(sample => sample.Count > 0 && sample.Max >= 0),
            "OBS-B2 queue wait histogram was not recorded.");
        ZlinkStreamAssert.Ensure(transfers.Any(sample => sample.Value >= 1),
            "OBS-B2 transfer counter did not increase.");
        ZlinkStreamAssert.Ensure(transferDuration.Any(sample => sample.Count > 0),
            "OBS-B2 transfer duration histogram was not recorded.");
        ZlinkStreamAssert.Ensure(pending.Any(sample => sample.Count > 0),
            "OBS-B2 pending-request sample was not recorded at transfer start.");
        await connector.Close.Async();
        Console.WriteLine("scenario OBS-B2 passed");
    }

    private static async Task<MetricSample[]> WaitMetricAsync(
        ScenarioContext context,
        string name,
        decimal minimum,
        IReadOnlyDictionary<string, string>? tags = null) =>
        (await context.PlayA.Post("/metrics/wait")
            .Body(new MetricWaitReq(name, minimum, RequiredTags: tags))
            .Async<MetricSample[]>()).Body;
}
