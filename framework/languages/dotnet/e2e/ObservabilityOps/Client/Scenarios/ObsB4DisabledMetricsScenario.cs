// Verifies OBS-B4 Disabled Metrics behavior.
using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsB4DisabledMetricsScenario
{
    private const int TrafficCount = 80;

    public static async Task RunAsync(ScenarioContext context)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var actorId = $"obs-b4-{suffix}";
        var roomRid = $"room-b4-{suffix}";
        // B4 follows B3's 11s store pause on the same topology: the first
        // store write can still sit behind the multiplexer's recovery, so the
        // room creation polls within a bounded window instead of racing it.
        var roomDeadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        while (true)
        {
            try
            {
                await context.PlayB.Post("/rooms").Body(new CreateRoomReq(roomRid)).AsyncRaw();
                break;
            }
            catch (Exception) when (DateTimeOffset.UtcNow < roomDeadline)
            {
                await Task.Delay(500);
            }
        }
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        // The fresh room's row publish races the join resolve after the B3
        // outage; poll the join within a bounded window.
        JoinRoomRes joined;
        var joinDeadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
        while (true)
        {
            try
            {
                joined = await connector.Request(new JoinRoomReq(roomRid)).Async<JoinRoomRes>();
                break;
            }
            catch (Exception) when (DateTimeOffset.UtcNow < joinDeadline)
            {
                await Task.Delay(300);
            }
        }
        ZlinkStreamAssert.Ensure(joined.NodeRid == "play-b", "OBS-B4 actor did not reach play-b.");
        for (var index = 0; index < TrafficCount; index++)
        {
            var marker = $"obs-b4-{index}";
            var action = await connector.Request(new GameActionReq(marker)).Async<GameActionRes>();
            ZlinkStreamAssert.Ensure(action.Marker == marker && action.NodeRid == "play-b",
                "OBS-B4 messaging changed while metrics were disabled.");
        }
        var evidence = (await context.PlayB.Get("/evidence").Async<EvidenceSnapshot>()).Body;
        ZlinkStreamAssert.Ensure(evidence.Metrics.Length == 0,
            "OBS-B4 node without a reader retained metric samples.");
        ZlinkStreamAssert.Ensure(evidence.Entries.Count(entry => entry.Contains("game-action|", StringComparison.Ordinal))
                                >= TrafficCount,
            "OBS-B4 disabled metrics changed message processing.");
        await connector.Close.Async();
        Console.WriteLine("scenario OBS-B4 passed");
    }
}
