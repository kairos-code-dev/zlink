using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD3Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var entryActorId = $"actor-sm-d3-entry-{Guid.NewGuid():N}";
        await using var entry = await ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq(entryActorId, "entry bind", "play-a"));
        var entryPushed = entry.WaitFor<ActorPushNotify>().Async().AsTask();
        var entryReply = await entry.Request(new ActorPushReq("entry-push"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var entryNotify = await entryPushed;
        ScenarioAssert.That(entryReply.ActorId == entryActorId, "SM-D3 entry bind actor mismatch.");
        ScenarioAssert.That(entryReply.NodeRid == "play-a", "SM-D3 entry bind node mismatch.");
        ScenarioAssert.That(entryNotify.Payload.ActorId == entryActorId, "SM-D3 entry push actor mismatch.");
        ScenarioAssert.That(entryNotify.Payload.Value == "entry-push", "SM-D3 entry push value mismatch.");

        var userSpotRid = $"spot-sm-d3-user-{Guid.NewGuid():N}";
        var userActorId = $"actor-sm-d3-user-{Guid.NewGuid():N}";
        await using var user = await ConnectAndUserSpotAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new UserSpotAuthReq(userSpotRid, userActorId, "user bind", "play-a"));
        var userPushed = user.WaitFor<ActorPushNotify>().Async().AsTask();
        var userReply = await user.Request(new ActorPingReq("user-relay"))
            .PacketName("UserActorPingReq")
            .Async<ActorPingReply>();
        var userPushReply = await user.Request(new ActorPushReq("user-push"))
            .PacketName("UserActorPushReq")
            .Async<ActorPingReply>();
        var userNotify = await userPushed;
        ScenarioAssert.That(userReply.ActorId == userActorId, "SM-D3 user bind actor mismatch.");
        ScenarioAssert.That(userReply.NodeRid == "play-a", "SM-D3 user bind node mismatch.");
        ScenarioAssert.That(userReply.SpotRid == userSpotRid, "SM-D3 user bind spot mismatch.");
        ScenarioAssert.That(userReply.Value == "user-relay", "SM-D3 user relay value mismatch.");
        ScenarioAssert.That(userPushReply.ActorId == userActorId, "SM-D3 user push reply actor mismatch.");
        ScenarioAssert.That(userNotify.Payload.ActorId == userActorId, "SM-D3 user push actor mismatch.");
        ScenarioAssert.That(userNotify.Payload.Value == "user-push", "SM-D3 user push value mismatch.");

        await EvidenceWait.ForAsync(
            playA,
            new EvidenceWaitRequest([
                $"spot-actor-joined|rid=play-a|spot={userSpotRid}|actor={userActorId}",
                $"actor-ping|rid=play-a|actor={userActorId}|spot={userSpotRid}|value=user-relay",
            ]),
            evidence => evidence.Any(line => line.Contains(
                    $"spot-actor-joined|rid=play-a|spot={userSpotRid}|actor={userActorId}",
                    StringComparison.Ordinal))
                && evidence.Any(line => line.Contains(
                    $"actor-ping|rid=play-a|actor={userActorId}|spot={userSpotRid}|value=user-relay",
                    StringComparison.Ordinal)),
            "SM-D3 expected user spot bind and relay evidence.");

        Console.WriteLine("operation SpotService.sm-d3 passed");
    }

    static Task<IZlinkStreamConnector> ConnectAndAuthWithRetryAsync(string endpoint, AuthReq auth)
    {
        return ConnectWithRetryAsync(
            endpoint,
            auth.ActorId,
            client => client.Request(auth).PacketName("AuthReq").Async<AuthReply>().AsTask());
    }

    static Task<IZlinkStreamConnector> ConnectAndUserSpotAuthWithRetryAsync(string endpoint, UserSpotAuthReq auth)
    {
        return ConnectWithRetryAsync(
            endpoint,
            auth.ActorId,
            client => client.Request(auth).PacketName("UserSpotAuthReq").Async<AuthReply>().AsTask());
    }

    static async Task<IZlinkStreamConnector> ConnectWithRetryAsync(
        string endpoint,
        string actorId,
        Func<IZlinkStreamConnector, Task> authenticate)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var client = CreateClient(endpoint);
            try
            {
                await client.Connect.Async();
                await authenticate(client);
                return client;
            }
            catch (Exception ex) when (ex is ZlinkStreamException or TimeoutException)
            {
                last = ex;
                await client.DisposeAsync();
                await Task.Delay(200);
            }
        }

        throw new InvalidOperationException(
            last is null ? $"Actor auth did not become routable: {actorId}" : $"Actor auth did not become routable: {actorId}. Last error: {last.Message}",
            last);
    }

    static IZlinkStreamConnector CreateClient(string endpoint)
    {
        ScenarioAssert.That(!string.IsNullOrWhiteSpace(endpoint), "session-a stream endpoint is required.");
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            ConnectTimeout = TimeSpan.FromSeconds(5),
            RequestTimeout = TimeSpan.FromSeconds(5),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false },
            DispatchMode = ZlinkStreamDispatchMode.Immediate,
            MaxReceivedMessages = 1024,
        });
    }

}
