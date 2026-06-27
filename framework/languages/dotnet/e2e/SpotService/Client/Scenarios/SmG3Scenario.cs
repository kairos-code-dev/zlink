using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

// Verifies concurrent actor join, request, and leave through real stream clients.
internal static class SmG3Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        const int actorCount = 2;
        var key = Guid.NewGuid().ToString("N");
        var spotRid = $"spot-sm-g3-{key}";
        var actorIds = Enumerable.Range(0, actorCount)
            .Select(index => $"actor-sm-g3-{key}-{index}")
            .ToArray();
        var clients = new List<IZlinkStreamConnector>();
        try
        {
            foreach (var actorId in actorIds)
            {
                var client = await ConnectAndAuthWithRetryAsync(sessionAStreamEndpoint, spotRid, actorId);
                clients.Add(client);
            }

            await Task.WhenAll(actorIds.Select(async (actorId, index) =>
            {
                var client = clients[index];
                var ping = await client.Request(new ActorPingReq(actorId))
                    .PacketName("UserActorPingReq")
                    .Async<ActorPingReply>();
                ScenarioAssert.That(ping.ActorId == actorId, "SM-G3 actor request target mismatch.");
                ScenarioAssert.That(ping.NodeRid == "play-a", "SM-G3 actor request reached the wrong node.");
                var left = await client.Request(new LeaveReq(actorId))
                    .PacketName("LeaveReq")
                    .Async<LeaveReply>();
                ScenarioAssert.That(left.Accepted && left.ActorId == actorId, "SM-G3 leave reply mismatch.");
            }));

            var expectedEvidence = actorIds
                .SelectMany(actorId => new[]
                {
                    $"spot-actor-joined|rid=play-a|spot={spotRid}|actor={actorId}",
                    $"spot-actor-left|rid=play-a|spot={spotRid}|actor={actorId}",
                })
                .ToArray();
            var evidence = await EvidenceWait.ForAllAsync(
                playA,
                expectedEvidence,
                "SM-G3 expected concurrent join and leave evidence.");
            foreach (var actorId in actorIds)
            {
                ScenarioAssert.That(
                    evidence.Any(line => line.Contains(
                        $"spot-actor-joined|rid=play-a|spot={spotRid}|actor={actorId}",
                        StringComparison.Ordinal)),
                    $"SM-G3 join evidence count mismatch for {actorId}.");
                ScenarioAssert.That(
                    evidence.Any(line => line.Contains(
                        $"spot-actor-left|rid=play-a|spot={spotRid}|actor={actorId}",
                        StringComparison.Ordinal)),
                    $"SM-G3 leave evidence count mismatch for {actorId}.");
            }
        }
        finally
        {
            foreach (var client in clients)
            {
                await client.DisposeAsync();
            }
        }

        Console.WriteLine("operation SpotService.sm-g3 passed");
    }

    static async Task<IZlinkStreamConnector> ConnectAndAuthWithRetryAsync(
        string endpoint,
        string spotRid,
        string actorId)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(15);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var client = CreateClient(endpoint);
            try
            {
                await client.Connect.Async();
                await client.Request(new UserSpotAuthReq(spotRid, actorId, actorId, "play-a"))
                    .PacketName("UserSpotAuthReq")
                    .Async<AuthReply>();
                return client;
            }
            catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await client.DisposeAsync();
            }

            await Task.Delay(250);
        }

        throw new InvalidOperationException(
            last is null
                ? $"Actor auth did not become routable: {actorId}"
                : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}",
            last);
    }

    static IZlinkStreamConnector CreateClient(string endpoint)
    {
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
        });
    }

}
