// Verifies OBS-C1 Draining Marker behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC1DrainingMarkerScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var actorId = $"obs-c1-{suffix}";
        var roomRid = $"room-c1-{suffix}";
        RoomRid = roomRid;
        await context.PlayA.Post("/rooms").Body(new CreateRoomReq(roomRid)).AsyncRaw();
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        await connector.Request(new JoinRoomReq(roomRid)).Async<JoinRoomRes>();
        await context.PlayA.Post("/drain?deadlineMs=30000").AsyncRaw();

        var draining = await WaitEvidenceAsync(context, snapshot =>
            !snapshot.Ready
            && snapshot.PeerRows.Any(row => row.NodeRid == "play-a" && row.Draining)
            && snapshot.SpotRows.Any(row => row.SpotRid == roomRid)
            && snapshot.Metrics.Any(sample => sample.Name == "zlink.drain.state"
                                              && sample.Value == 1
                                              && sample.Tags.GetValueOrDefault("state") == "draining"));
        ZlinkStreamAssert.Ensure(draining.PeerRows.Any(row => row.NodeRid == "play-a" && row.Draining),
            "OBS-C1 typed draining peer row was not published.");
        var action = await connector.Request(new GameActionReq("obs-c1-existing-session"))
            .Async<GameActionRes>();
        ZlinkStreamAssert.Ensure(action.ActorId == actorId,
            "OBS-C1 existing bound session request failed during drain propagation.");
        ZlinkStreamAssert.Ensure((await EvidenceAsync(context)).SpotRows.Any(row => row.SpotRid == roomRid),
            "OBS-C1 drain-natural room disappeared before natural close.");
        await connector.Close.Async();
        Console.WriteLine("scenario OBS-C1 passed");
    }

    private static async Task<EvidenceSnapshot> WaitEvidenceAsync(
        ScenarioContext context,
        Func<EvidenceSnapshot, bool> predicate)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = await EvidenceAsync(context);
            if (predicate(snapshot)) return snapshot;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C1 draining evidence did not converge.");
    }

    private static async Task<EvidenceSnapshot> EvidenceAsync(ScenarioContext context) =>
        (await context.PlayA.Get("/evidence").Query("spotRid", RoomRid!)
            .Async<EvidenceSnapshot>()).Body;

    private static string? RoomRid;
}
