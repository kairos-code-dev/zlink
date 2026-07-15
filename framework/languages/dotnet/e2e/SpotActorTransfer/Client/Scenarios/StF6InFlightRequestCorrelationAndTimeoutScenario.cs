// Verifies ST-F6 In Flight Request Correlation And Timeout behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StF6InFlightRequestCorrelationAndTimeoutScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        await RunInFlightRequestCorrelationAsync(context);
        await RunInFlightRequestTimeoutAsync(context);
    }

    private static async Task RunInFlightRequestCorrelationAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-inflight-req-{Guid.NewGuid():N}";
        var spotRid = $"spot-inflight-req-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid, "delay-joined");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 106);
        var oldRef = await context.GetActorRefAsync(context.NodeA, actorId);

        var joinTask = context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-F6", spotRid));
        await context.WaitEvidenceAsync(context.NodeB, [$"ST-F6|{actorId}|joined_wait|{spotRid}"]);
        var requestTask = context.ProbeRefAsync(
            context.NodeA,
            actorId,
            oldRef,
            new ProbeReq("ST-F6", "correlated-reply"),
            TimeSpan.FromSeconds(5));
        await context.WaitRuntimeEvidenceAsync(context.NodeA,
            $"handoff_backlog actor={actorId} arrival=0");
        await context.ReleaseJoinedGateAsync(context.NodeB, spotRid);

        ZlinkStreamAssert.Ensure((await joinTask).Accepted, "ST-F6 correlation transfer was rejected.");
        var response = await requestTask;
        ZlinkStreamAssert.Ensure(response.Succeeded && response.Reply?.NodeRid == "actor-b",
            $"ST-F6 reply did not correlate to the original caller: {response.ErrorKind}");
        ZlinkStreamAssert.Ensure(response.Reply?.Marker == "correlated-reply", "ST-F6 correlated reply marker mismatch.");
        await context.WaitEvidenceAsync(context.NodeB, [$"ST-F6|{actorId}|packet_handler|correlated-reply"]);
    }

    private static async Task RunInFlightRequestTimeoutAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-inflight-req-timeout-{Guid.NewGuid():N}";
        var spotRid = $"spot-inflight-req-timeout-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid, "delay-joined");
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 107);
        var oldRef = await context.GetActorRefAsync(context.NodeA, actorId);

        var joinTask = context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-F6", spotRid));
        await context.WaitEvidenceAsync(context.NodeB, [$"ST-F6|{actorId}|joined_wait|{spotRid}"]);
        var requestTask = context.ProbeRefAsync(
            context.NodeA,
            actorId,
            oldRef,
            new ProbeReq("ST-F6", "late-reply"),
            TimeSpan.FromMilliseconds(250));
        var timeout = await requestTask;
        ZlinkStreamAssert.Ensure(!timeout.Succeeded && timeout.ErrorKind == nameof(TimeoutException),
            $"ST-F6 expected normal TimeoutException, got '{timeout.ErrorKind}'.");

        await context.ReleaseJoinedGateAsync(context.NodeB, spotRid);
        ZlinkStreamAssert.Ensure((await joinTask).Accepted, "ST-F6 timeout transfer was rejected.");
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-F6|{actorId}|packet_handler|late-reply",
            $"ST-F6|{actorId}|late_reply_created|late-reply"
        ]);
    }
}
