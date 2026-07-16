// Verifies SM-G2 SpotNode scale-out and explicit placement behavior.
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmG2OwnerSpotRemapScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient playA,
        ZLinkHttpClient playB,
        ZLinkHttpClient gateway)
    {
        var suffix = Guid.NewGuid().ToString("N");
        var existingSpotRid = $"spot-sm-g2-existing-{suffix}";
        var existingActorId = $"actor-sm-g2-existing-{suffix}";
        var newActorId = $"actor-sm-g2-new-{suffix}";
        var newSpotRid = $"spot-sm-g2-new-{suffix}";

        var existingSpot = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(existingSpotRid)).Async<CreateSpotRes>()).Body;
        await gateway.Post("/node/wait-ready")
            .Body(new NodeReadinessWaitReq("play-a"))
            .Async<NodeReadinessWaitRes>();
        var existingActor = (await gateway.Post("/entry/join")
            .Body(new EntryJoinRouteReq("play-a",
                new JoinReq("before-scale-out", existingActorId, "existing", 1, [])))
            .Async<JoinRes>()).Body;
        ZlinkStreamAssert.Ensure(existingSpot.NodeRid == "play-a" && existingActor.NodeRid == "play-a",
            "SM-G2 baseline owner was not placed on play-a.");

        Console.WriteLine("spot-service sm-g2 scale-out-ready");
        var readiness = (await gateway.Post("/node/wait-ready")
            .Body(new NodeReadinessWaitReq("play-b"))
            .Async<NodeReadinessWaitRes>()).Body;
        ZlinkStreamAssert.Ensure(readiness.PeerReady && readiness.EntrySpotReady,
            "SM-G2 play-b peer and Entry Spot readiness did not converge.");

        var existingFollowUp = (await playA.Post("/spot/state/request")
            .Body(new SpotStateRouteReq(existingSpotRid, "add", 1))
            .Async<StateRes>()).Body;
        var newActor = (await gateway.Post("/entry/join")
            .Body(new EntryJoinRouteReq("play-b",
                new JoinReq("after-scale-out", newActorId, "new", 1, [])))
            .Async<JoinRes>()).Body;
        var newSpot = (await playB.Post("/spot/create")
            .Body(new CreateSpotReq(newSpotRid)).Async<CreateSpotRes>()).Body;

        ZlinkStreamAssert.Ensure(existingFollowUp.NodeRid == "play-a",
            "SM-G2 scale-out changed the existing Spot owner.");
        ZlinkStreamAssert.Ensure(newActor.NodeRid == "play-b",
            "SM-G2 application JoinReq did not create the actor on play-b.");
        ZlinkStreamAssert.Ensure(newSpot.NodeRid == "play-b",
            "SM-G2 play-b local SpotManager did not create the new Spot locally.");
        Console.WriteLine("operation SpotService.sm-g2 passed");
    }

}
