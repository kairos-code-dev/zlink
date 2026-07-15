// Verifies ST-D2 Stale Source Release Fencing behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StD2StaleSourceReleaseFencingScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-stale-release-{Guid.NewGuid():N}";
        var spotRid = $"spot-stale-release-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 81);

        var join = await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-D2", spotRid));
        ZlinkStreamAssert.Ensure(join.Accepted, "ST-D2 join was rejected.");
        var before = await context.GetActorRefAsync(context.NodeB, actorId);
        ZlinkStreamAssert.Ensure(before.NodeRid == "actor-b", $"ST-D2 target ref expected actor-b, got {before.NodeRid}.");

        await Task.Delay(TimeSpan.FromSeconds(2));
        var after = await context.GetActorRefAsync(context.NodeB, actorId);
        ZlinkStreamAssert.Ensure(after.NodeRid == "actor-b", $"ST-D2 target ref changed after delayed cleanup: {after.NodeRid}.");
        ZlinkStreamAssert.Ensure(after.Generation == before.Generation,
            $"ST-D2 generation changed after delayed cleanup. before={before.Generation}, after={after.Generation}");
        var probe = await context.ProbeAsync(context.NodeB, actorId, new ProbeReq("ST-D2", "after-stale-cleanup-window"));
        ZlinkStreamAssert.Ensure(probe.NodeRid == "actor-b", $"ST-D2 probe expected actor-b, got {probe.NodeRid}.");
    }
}
