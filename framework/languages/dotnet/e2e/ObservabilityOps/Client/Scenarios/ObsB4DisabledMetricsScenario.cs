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
        await context.PlayB.Post("/rooms").Body(new CreateRoomReq(roomRid)).AsyncRaw();
        await using var connector = await context.ConnectAsync();
        await connector.Request(new AuthenticateReq(actorId)).Async<AuthenticateRes>();
        var joined = await connector.Request(new JoinRoomReq(roomRid)).Async<JoinRoomRes>();
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
