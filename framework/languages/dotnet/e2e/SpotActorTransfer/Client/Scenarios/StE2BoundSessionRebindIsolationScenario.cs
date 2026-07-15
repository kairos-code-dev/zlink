// Verifies ST-E2 Bound Session Rebind Isolation behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StE2BoundSessionRebindIsolationScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-bound-session-rebind-{Guid.NewGuid():N}";
        var spotRid = $"spot-bound-session-rebind-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 92);
        var sourceRef = await context.GetActorRefAsync(context.NodeA, actorId);
        await using var oldSession = await context.ConnectAndBindAsync(context.Options.NodeAStreamEndpoint, "ST-E2", sourceRef);
        var beforeTransferPush = oldSession.WaitFor<BoundPushNotify>()
            .Where(message => message.Payload.Marker == "before-rebind-transfer")
            .Timeout(TimeSpan.FromSeconds(10))
            .Async().AsTask();
        await context.BoundPushAsync(context.NodeA, actorId, new BoundPushReq("ST-E2", "before-rebind-transfer"));
        await beforeTransferPush;

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-E2", spotRid));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-E2 join was rejected.");
        var targetRef = await context.GetActorRefAsync(context.NodeB, actorId);
        await using var newSession = await context.ConnectAndBindAsync(context.Options.NodeBStreamEndpoint, "ST-E2", targetRef);

        var oldPush = oldSession.ExpectNone<BoundPushNotify>()
            .Within(TimeSpan.FromMilliseconds(500))
            .Async().AsTask();
        var newPush = newSession.WaitFor<BoundPushNotify>()
            .Where(message => message.Payload.Marker == "after-rebind")
            .Timeout(TimeSpan.FromSeconds(10))
            .Async().AsTask();
        var pushReply = await context.BoundPushAsync(context.NodeB, actorId, new BoundPushReq("ST-E2", "after-rebind"));
        var notify = await newPush;
        ZlinkStreamAssert.Ensure(pushReply.NodeRid == "actor-b", $"ST-E2 bound push reply expected actor-b, got {pushReply.NodeRid}.");
        ZlinkStreamAssert.Ensure(notify.Payload.Marker == "after-rebind", "ST-E2 new bound session notify marker mismatch.");
        await oldPush;
    }
}
