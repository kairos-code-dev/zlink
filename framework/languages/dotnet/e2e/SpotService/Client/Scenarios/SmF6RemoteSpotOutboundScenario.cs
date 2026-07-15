using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

// Verifies that remote spot request/send and actor join work when the servers
// register only SpotMesh nodes and no RouteMesh channels.
internal static class SmF6RemoteSpotOutboundScenario
{
    public static async Task RunAsync(ZLinkHttpClient multiA, ZLinkHttpClient multiB)
    {
        var sourceSpotRid = $"spot-sm-f6-source-{Guid.NewGuid():N}";
        var targetSpotRid = $"spot-sm-f6-target-{Guid.NewGuid():N}";
        var actorId = $"actor-sm-f6-{Guid.NewGuid():N}";
        var marker = $"sm-f6-{Guid.NewGuid():N}";

        var target = (await multiB.Post("/spot/create-user-local")
            .Body(new CreateSpotReq(targetSpotRid))
            .Async<CreateSpotRes>()).Body;
        ZlinkStreamAssert.Ensure(target.NodeRid == SpotServiceNames.MultiSpotNodeB,
            "SM-F6 target spot was not created on node B.");

        var mesh = (await multiA.Post("/spot/spot-only/request-send")
            .Body(new SpotOnlyMeshReq(sourceSpotRid, targetSpotRid, marker))
            .Async<SpotOnlyMeshRes>()).Body;
        ZlinkStreamAssert.Ensure(mesh.TargetSpotRid == targetSpotRid, "SM-F6 request target mismatch.");
        ZlinkStreamAssert.Ensure(mesh.TargetValue == 7, "SM-F6 target request value mismatch.");

        var join = (await multiA.Post("/actor/spot-only-join")
            .Body(new SpotOnlyJoinReq(targetSpotRid, actorId, marker))
            .Async<SpotOnlyJoinRes>()).Body;
        ZlinkStreamAssert.Ensure(join.Accepted, "SM-F6 spot-only actor join was rejected.");
        ZlinkStreamAssert.Ensure(join.ActorId == actorId, "SM-F6 actor join id mismatch.");

        var evidence = (await multiB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([
                $"spot-state-request|rid={SpotServiceNames.MultiSpotNodeB}|spot={targetSpotRid}|value=7",
                $"spot-state-command|rid={SpotServiceNames.MultiSpotNodeB}|spot={targetSpotRid}|marker=sm-f6-send-{marker}",
                $"spot-actor-joined|rid={SpotServiceNames.MultiSpotNodeB}|spot={targetSpotRid}|actor={actorId}"
            ]))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            evidence.Any(line => line.Contains(
                $"spot-actor-joined|rid={SpotServiceNames.MultiSpotNodeB}|spot={targetSpotRid}|actor={actorId}",
                StringComparison.Ordinal)),
            "SM-F6 remote actor join evidence did not appear on node B.");
        Console.WriteLine("operation SpotService.sm-f6 passed");
    }
}
