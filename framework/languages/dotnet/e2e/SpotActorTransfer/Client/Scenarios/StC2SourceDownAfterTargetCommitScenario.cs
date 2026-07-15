using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StC2SourceDownAfterTargetCommitScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-source-down-after-commit-{Guid.NewGuid():N}";
        var spotRid = $"spot-source-down-after-commit-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 61);
        var sourceRef = await context.GetActorRefAsync(context.NodeA, actorId);
        await using var bound = await context.ConnectAndBindAsync(context.Options.NodeBStreamEndpoint, "ST-C2", sourceRef);
        var beforeTransferPush = context.WaitBoundPushAsync(bound, "bound-before-transfer");
        var beforeTransferReply = await bound.Request(new BoundPushReq("ST-C2", "bound-before-transfer"))
            .PacketName(nameof(BoundPushReq))
            .Async<BoundPushRes>();
        ZlinkStreamAssert.Ensure(beforeTransferReply.NodeRid == "actor-a", $"ST-C2 pre-transfer bound push expected actor-a, got {beforeTransferReply.NodeRid}.");
        await beforeTransferPush;

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-C2", spotRid));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-C2 join was rejected.");
        await context.WaitEvidenceAsync(context.NodeB, [
            $"transfer|{actorId}|transfer_in|61",
            $"transfer|{actorId}|joined|{spotRid}:61"
        ]);
        var beforeShutdown = await context.GetActorRefAsync(context.NodeB, actorId);
        ZlinkStreamAssert.Ensure(beforeShutdown.NodeRid == "actor-b", $"ST-C2 target ref expected actor-b, got {beforeShutdown.NodeRid}.");

        await context.ShutdownAsync(context.NodeA);
        await Task.Delay(TimeSpan.FromSeconds(2));

        var afterShutdown = await context.GetActorRefAsync(context.NodeB, actorId);
        ZlinkStreamAssert.Ensure(afterShutdown.NodeRid == "actor-b", $"ST-C2 target ref changed after source shutdown: {afterShutdown.NodeRid}.");
        ZlinkStreamAssert.Ensure(
            afterShutdown.Generation == beforeShutdown.Generation,
            $"ST-C2 target generation changed after source shutdown. before={beforeShutdown.Generation}, after={afterShutdown.Generation}");

        var probe = await context.ProbeAsync(context.NodeB, actorId, new ProbeReq("ST-C2", "after-source-down"));
        ZlinkStreamAssert.Ensure(probe.NodeRid == "actor-b", $"ST-C2 probe expected actor-b, got {probe.NodeRid}.");
        ZlinkStreamAssert.Ensure(probe.SpotRid == spotRid, "ST-C2 probe did not reach target spot after source shutdown.");
        var pushed = context.WaitBoundPushAsync(bound, "bound-after-source-down");
        var pushReply = await context.BoundPushAsync(context.NodeB, actorId, new BoundPushReq("ST-C2", "bound-after-source-down"));
        var notify = await pushed;
        ZlinkStreamAssert.Ensure(pushReply.NodeRid == "actor-b", $"ST-C2 bound push reply expected actor-b, got {pushReply.NodeRid}.");
        ZlinkStreamAssert.Ensure(notify.Payload.NodeRid == "actor-b", $"ST-C2 bound push notify expected actor-b, got {notify.Payload.NodeRid}.");
        ZlinkStreamAssert.Ensure(notify.Payload.Marker == "bound-after-source-down", "ST-C2 bound push notify marker mismatch.");
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-C2|{actorId}|packet_handler|after-source-down",
            $"ST-C2|{actorId}|bound_push|bound-after-source-down"
        ]);
    }
}
