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
        await connector.Request(new JoinRoomReq(roomRid)).Async<JoinRoomRes>();
        await connector.Request(new ReturnToLobbyReq("obs-c5-room-complete")).Async<ReturnToLobbyRes>();
        await context.PlayA.Post($"/rooms/{roomRid}/close").AsyncRaw();
        await context.PlayA.Post("/drain?deadlineMs=30000").AsyncRaw();
        await WaitActorAsync(context, actorId, "play-b", TimeSpan.FromSeconds(15));
        var status = await ScenarioContext.WaitForDrainAsync(
            context.PlayA, TimeSpan.FromSeconds(35));
        ZlinkStreamAssert.Ensure(status.Result == "Drained",
            $"OBS-C5 sequential rollout returned {status.Result}/{status.Reason}.");
        var metrics = (await context.PlayA.Get("/evidence").Async<EvidenceSnapshot>()).Body.Metrics;
        ZlinkStreamAssert.Ensure(metrics.All(sample => sample.Name != "zlink.drain.forced"),
            "OBS-C5 sequential rollout entered ForceStopping.");
        await connector.Close.Async();
    }

    private static async Task RunSimultaneousAsync(ScenarioContext context)
    {
        var actorId = $"obs-c5-zero-target-{Guid.NewGuid():N}";
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        await Task.WhenAll(
            context.PlayA.Post("/drain?deadlineMs=10000").AsyncRaw().AsTask(),
            context.PlayB.Post("/drain?deadlineMs=10000").AsyncRaw().AsTask());
        var draining = await WaitBothDrainingAsync(context);
        ZlinkStreamAssert.Ensure(draining.ActorRows.Any(row => row.ActorId == actorId && row.NodeRid == "play-a"),
            "OBS-C5 zero-target drain moved the actor to a draining peer.");
        var source = await ScenarioContext.WaitForDrainAsync(
            context.PlayA, TimeSpan.FromSeconds(15));
        ZlinkStreamAssert.Ensure(source.Result == "ForceStopped" && source.Reason == "DeadlineExceeded",
            $"OBS-C5 zero-target source returned {source.Result}/{source.Reason}.");
        var metrics = (await context.PlayA.Get("/evidence").Async<EvidenceSnapshot>()).Body.Metrics;
        ZlinkStreamAssert.Ensure(metrics.Any(sample => sample.Name == "zlink.drain.forced"
                                                      && sample.Tags.GetValueOrDefault("kind") == "actor"
                                                      && sample.Value >= 1),
            "OBS-C5 zero-target forced actor metric was not recorded.");
    }

    private static async Task<EvidenceSnapshot> WaitBothDrainingAsync(ScenarioContext context)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(8);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = (await context.PlayA.Get("/evidence").Async<EvidenceSnapshot>()).Body;
            if (snapshot.PeerRows.Any(row => row.NodeRid == "play-a" && row.Draining)
                && snapshot.PeerRows.Any(row => row.NodeRid == "play-b" && row.Draining))
                return snapshot;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C5 simultaneous draining markers did not converge.");
    }

    private static async Task WaitActorAsync(
        ScenarioContext context, string actorId, string nodeRid, TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = (await context.PlayB.Get("/evidence").Async<EvidenceSnapshot>()).Body;
            if (snapshot.ActorRows.Any(row => row.ActorId == actorId && row.NodeRid == nodeRid)) return;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C5 sequential actor location did not converge.");
    }

}
