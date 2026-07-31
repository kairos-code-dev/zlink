// Verifies OBS-C1 Draining Marker behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC1DrainingMarkerScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        _ = await context.PlayNodeIdAsync("play-a");
        _ = await context.PlayNodeIdAsync("play-b");
        var suffix = Guid.NewGuid().ToString("N");
        var actorId = $"obs-c1-{suffix}";
        var roomPrefix = $"room-c1-{suffix}";
        // The scenario reads host-state metrics from the node it relocates, and
        // play-b runs with metrics disabled on purpose for OBS-B4, so the room
        // has to sit on play-a.
        var room = await context.CreateRoomOnObservedNodeAsync("play-a", roomPrefix);
        var roomRid = room.RoomRid;
        RoomRid = roomRid;
        var source = context.Play(room.NodeRid);
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        await context.JoinRoomAsync(connector, actorId, roomRid);
        await source.Post("/relocate?deadlineMs=30000").AsyncRaw();

        var draining = await WaitEvidenceAsync(source, snapshot =>
            !snapshot.Ready
            && snapshot.PeerRows.Any(row =>
                row.NodeRid == room.NodeRid && row.Draining)
            && snapshot.SpotRows.Any(row => row.SpotRid == roomRid)
            // The relocating state can pass faster than any snapshot poll, so
            // spec 24 §3 exposes state changes as a stream. The host records
            // every observed status, and this reads that record instead of
            // racing the live gauge.
            && snapshot.Entries.Any(line =>
                line.Contains("host-state|", StringComparison.Ordinal)
                && line.Contains("state=Relocating", StringComparison.Ordinal)));
        ZlinkStreamAssert.Ensure(
            draining.PeerRows.Any(row =>
                row.NodeRid == room.NodeRid && row.Draining),
            "OBS-C1 typed draining peer row was not published.");
        var action = await connector.Request(new GameActionReq("obs-c1-existing-session"))
            .Async<GameActionRes>();
        ZlinkStreamAssert.Ensure(action.ActorId == actorId,
            "OBS-C1 existing bound session request failed during drain propagation.");
        ZlinkStreamAssert.Ensure(
            (await EvidenceAsync(source)).SpotRows.Any(row => row.SpotRid == roomRid),
            "OBS-C1 room disappeared before relocation commit.");
        await connector.Close.Async();
        Console.WriteLine("scenario OBS-C1 passed");
    }

    private static async Task<EvidenceSnapshot> WaitEvidenceAsync(
        ZLinkHttpClient source,
        Func<EvidenceSnapshot, bool> predicate)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = await EvidenceAsync(source);
            if (predicate(snapshot)) return snapshot;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C1 draining evidence did not converge.");
    }

    private static async Task<EvidenceSnapshot> EvidenceAsync(ZLinkHttpClient source) =>
        (await source.Get("/evidence").Query("spotRid", RoomRid!)
            .Async<EvidenceSnapshot>()).Body;

    private static string? RoomRid;
}
