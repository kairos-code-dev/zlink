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
        await connector.Request(new JoinRoomReq(roomRid)).Async<JoinRoomRes>();
        var requests = Enumerable.Range(0, 3)
            .Select(index => connector.Request(new GameActionReq($"obs-c2-pending-{index}", 100))
                .Async<GameActionRes>().AsTask()).ToArray();
        var replies = await Task.WhenAll(requests);
        ScenarioContext.Require(replies.All(reply => reply.ActorId == actorId),
            "OBS-C2 a request lost its original reply during handoff.");
        var lobby = await connector.Request(new ReturnToLobbyReq("obs-c2-room-complete"))
            .Async<ReturnToLobbyRes>();
        ScenarioContext.Require(lobby.NodeRid == "play-a",
            "OBS-C2 actor did not leave the completed room on play-a.");
        await context.PlayA.Post($"/rooms/{roomRid}/close").AsyncRaw();
        await context.PlayA.Post("/drain?deadlineMs=30000").AsyncRaw();
        var location = await WaitActorLocationAsync(context, actorId);
        ScenarioContext.Require(location.ActorRows.Any(row => row.ActorId == actorId && row.NodeRid == "play-b"),
            "OBS-C2 actor location did not commit to play-b.");
        var result = await ScenarioContext.WaitForDrainAsync(
            context.PlayA, TimeSpan.FromSeconds(40));
        var metrics = (await context.PlayA.Get("/evidence").Async<EvidenceSnapshot>()).Body.Metrics;
        var forced = string.Join(",", metrics
            .Where(sample => sample.Name == "zlink.drain.forced")
            .Select(sample => $"{sample.Tags.GetValueOrDefault("kind")}={sample.Value}"));
        ScenarioContext.Require(result.Result == "Drained",
            $"OBS-C2 serving target handoff did not drain cleanly: {result.Result}/{result.Reason}; forced={forced}.");
        ScenarioContext.Require(connector.ReceivedCount(nameof(PlayerMovedNotify)) > 0,
            "OBS-C2 play-b push did not reach the connector receive queue.");
        PlayerMovedNotify? moved = null;
        while (connector.ReceivedCount(nameof(PlayerMovedNotify)) > 0)
        {
            var candidate = await connector.WaitFor<PlayerMovedNotify>().Async();
            if (candidate.Payload.ActorId == actorId && candidate.Payload.TargetNodeRid == "play-b")
            {
                moved = candidate.Payload;
                break;
            }
        }
        ScenarioContext.Require(moved?.TargetNodeRid == "play-b",
            "OBS-C2 bound session did not receive the play-b handoff notification.");
        ScenarioContext.Require(metrics.Any(sample => sample.Name == "zlink.drain.actors.handed_off"
                                                      && sample.Value >= 1),
            "OBS-C2 actor handoff metric did not increase.");
        ScenarioContext.Require(metrics.Any(sample => sample.Name == "zlink.actor.transfer.pending_requests.count"
                                                      && sample.Count >= 1),
            "OBS-C2 pending-request sample was not recorded once for handoff.");
        await connector.Close.Async();
        Console.WriteLine("scenario OBS-C2 passed");
    }

    private static async Task<EvidenceSnapshot> WaitActorLocationAsync(ScenarioContext context, string actorId)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
        while (DateTimeOffset.UtcNow < deadline)
        {
            var snapshot = (await context.PlayB.Get("/evidence").Async<EvidenceSnapshot>()).Body;
            if (snapshot.ActorRows.Any(row => row.ActorId == actorId && row.NodeRid == "play-b")) return snapshot;
            await Task.Delay(100);
        }
        throw new TimeoutException("OBS-C2 actor handoff location did not converge.");
    }

}
