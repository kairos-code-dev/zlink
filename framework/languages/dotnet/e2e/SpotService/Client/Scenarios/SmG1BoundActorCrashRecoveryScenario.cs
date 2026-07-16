// Verifies SM-G1 crash isolation and both explicit application recovery paths.
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmG1BoundActorCrashRecoveryScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient playAHttp,
        ZLinkHttpClient gateway,
        string sessionAStreamEndpoint,
        string sessionBStreamEndpoint)
    {
        const string crashedActorId = "actor-sm-g1-crash";
        const string survivorActorId = "actor-sm-g1-survivor";
        await using var ownerSession = await ConnectAsync(sessionAStreamEndpoint);
        await using var survivorSession = await ConnectAsync(sessionBStreamEndpoint);
        await ownerSession.Request(new AuthReq(crashedActorId, "snapshot-v1", "play-a"))
            .PacketName("AuthReq").Async<AuthRes>();
        await survivorSession.Request(new AuthReq(survivorActorId, "survivor", "play-b"))
            .PacketName("AuthReq").Async<AuthRes>();

        var oldRef = await CaptureRefAsync(gateway, crashedActorId);
        var inFlight = gateway.Post("/actor/request-ref")
            .Body(new ActorRefRequestReq(oldRef, "in-flight-1", 10000, 3000))
            .Async<ActorRefRequestRes>().AsTask();
        await playAHttp.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([
                $"actor-slow-ping-started|rid=play-a|actor={crashedActorId}|value=in-flight-1"
            ])).AsyncRaw();
        Console.WriteLine("spot-service sm-g1 crash-1-ready");

        var inFlightOutcome = (await inFlight).Body;
        EnsureInFlightFailure(inFlightOutcome, "first crash");
        await WaitMissingAsync(gateway, crashedActorId);
        await ZlinkStreamAssert.ExpectFailureAsync(async cancellationToken =>
        {
            _ = await ownerSession.Request(new ActorPingReq("crashed-session"))
                .PacketName("ActorPingReq")
                .Async<ActorPingRes>(cancellationToken);
        });
        await EnsureErrorAsync(gateway, oldRef, "before-restart", "ActorRouteNotFound");
        await EnsureSurvivorAsync(survivorSession, survivorActorId, "after-first-crash");

        Console.WriteLine("spot-service sm-g1 restart-1-ready");
        await gateway.Post("/node/wait-ready")
            .Body(new NodeReadinessWaitReq("play-a", 15000)).Async<NodeReadinessWaitRes>();
        await gateway.Post("/entry/join")
            .Body(new EntryJoinRouteReq("play-a",
                new JoinReq("replay-snapshot-v1", crashedActorId, "snapshot-v1", 1, ["replayed"])))
            .Async<JoinRes>();
        var restartedRef = await CaptureRefAsync(gateway, crashedActorId);
        ZlinkStreamAssert.Ensure(restartedRef.Generation != oldRef.Generation,
            "SM-G1 same-rid restart reused the previous actor generation.");
        await EnsureErrorAsync(gateway, oldRef, "stale-after-restart", "ActorLocationStale");
        await EnsureSuccessAsync(gateway, restartedRef, "restart-follow-up", "play-a");
        await ownerSession.Request(new AuthReq(crashedActorId, "snapshot-v1", "play-a"))
            .PacketName("AuthReq").Async<AuthRes>();

        var secondInFlight = gateway.Post("/actor/request-ref")
            .Body(new ActorRefRequestReq(restartedRef, "in-flight-2", 10000, 3000))
            .Async<ActorRefRequestRes>().AsTask();
        await playAHttp.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([
                $"actor-slow-ping-started|rid=play-a|actor={crashedActorId}|value=in-flight-2"
            ])).AsyncRaw();
        Console.WriteLine("spot-service sm-g1 crash-2-ready");
        EnsureInFlightFailure((await secondInFlight).Body, "second crash");
        await WaitMissingAsync(gateway, crashedActorId);
        await EnsureErrorAsync(gateway, restartedRef, "before-play-b-recovery", "ActorRouteNotFound");

        await gateway.Post("/node/wait-ready")
            .Body(new NodeReadinessWaitReq("play-b", 15000)).Async<NodeReadinessWaitRes>();
        await gateway.Post("/entry/join")
            .Body(new EntryJoinRouteReq("play-b",
                new JoinReq("replay-snapshot-v1", crashedActorId, "snapshot-v1", 1, ["replayed"])))
            .Async<JoinRes>();
        var recoveredRef = await CaptureRefAsync(gateway, crashedActorId);
        ZlinkStreamAssert.Ensure(recoveredRef.NodeRid == "play-b",
            "SM-G1 application recovery did not place the actor on play-b.");
        await EnsureErrorAsync(gateway, restartedRef, "stale-after-remap", "ActorLocationStale");
        await EnsureSuccessAsync(gateway, recoveredRef, "play-b-follow-up", "play-b");
        await using var rebound = await ConnectAsync(sessionBStreamEndpoint);
        await rebound.Request(new AuthReq(crashedActorId, "snapshot-v1", "play-b"))
            .PacketName("AuthReq").Async<AuthRes>();
        var reboundReply = await rebound.Request(new ActorPingReq("rebound"))
            .PacketName("ActorPingReq").Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(reboundReply.NodeRid == "play-b",
            "SM-G1 explicit rebind did not restore messaging on play-b.");
        await EnsureSurvivorAsync(survivorSession, survivorActorId, "after-second-crash");
        Console.WriteLine("operation SpotService.sm-g1 passed");
    }

    private static async Task<IZlinkStreamConnector> ConnectAsync(string endpoint)
    {
        var connector = ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
            RequestTimeout = TimeSpan.FromSeconds(10),
            Heartbeat = new ZlinkStreamHeartbeatOptions { Enabled = false }
        });
        await connector.Connect.Async();
        return connector;
    }

    private static async Task<ActorRefSnapshotRes> CaptureRefAsync(
        ZLinkHttpClient gateway, string actorId) =>
        (await gateway.Post("/actor/capture-ref")
            .Body(new ActorRefSnapshotReq(actorId)).Async<ActorRefSnapshotRes>()).Body;

    private static async Task WaitMissingAsync(ZLinkHttpClient gateway, string actorId) =>
        await gateway.Post("/actor/wait-missing")
            .Body(new ActorMissingWaitReq(actorId, 15000)).AsyncRaw();

    private static async Task EnsureErrorAsync(
        ZLinkHttpClient gateway,
        ActorRefSnapshotRes actor,
        string value,
        string expected)
    {
        var outcome = (await gateway.Post("/actor/request-ref")
            .Body(new ActorRefRequestReq(actor, value))
            .Async<ActorRefRequestRes>()).Body;
        ZlinkStreamAssert.Ensure(!outcome.Succeeded && outcome.ErrorKind == expected,
            $"SM-G1 expected {expected}, got success={outcome.Succeeded} kind={outcome.ErrorKind}.");
    }

    private static async Task EnsureSuccessAsync(
        ZLinkHttpClient gateway,
        ActorRefSnapshotRes actor,
        string value,
        string nodeRid)
    {
        var outcome = (await gateway.Post("/actor/request-ref")
            .Body(new ActorRefRequestReq(actor, value))
            .Async<ActorRefRequestRes>()).Body;
        ZlinkStreamAssert.Ensure(outcome.Succeeded && outcome.Reply?.NodeRid == nodeRid,
            $"SM-G1 live ActorRef follow-up failed: {outcome.ErrorKind}.");
    }

    private static void EnsureInFlightFailure(ActorRefRequestRes outcome, string phase) =>
        ZlinkStreamAssert.Ensure(!outcome.Succeeded
                                 && outcome.ErrorKind is "RouteNotConnected" or "Timeout",
            $"SM-G1 {phase} in-flight outcome was {outcome.ErrorKind}.");

    private static async Task EnsureSurvivorAsync(
        IZlinkStreamConnector connector, string actorId, string marker)
    {
        var reply = await connector.Request(new ActorPingReq(marker))
            .PacketName("ActorPingReq").Async<ActorPingRes>();
        ZlinkStreamAssert.Ensure(reply.ActorId == actorId && reply.NodeRid == "play-b",
            "SM-G1 survivor actor/session was affected by play-a crash.");
    }

}
