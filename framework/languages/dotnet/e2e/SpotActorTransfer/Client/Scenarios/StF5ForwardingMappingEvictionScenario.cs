// Verifies ST-F5 Forwarding Mapping Eviction behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StF5ForwardingMappingEvictionScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-map-chain-{Guid.NewGuid():N}";
        var spotB = $"spot-map-chain-b-{Guid.NewGuid():N}";
        var spotC = $"spot-map-chain-c-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotB);
        await context.CreateSpotAsync(context.NodeC, spotC);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 105);
        var oldRefA = await context.GetActorRefAsync(context.NodeA, actorId);
        ZlinkStreamAssert.Ensure((await context.JoinAsync(context.NodeA, actorId, new JoinTargetReq("ST-F5", spotB))).Accepted,
            "ST-F5 first transfer was rejected.");
        var oldRefB = await context.GetActorRefAsync(context.NodeB, actorId);
        ZlinkStreamAssert.Ensure((await context.JoinAsync(context.NodeB, actorId, new JoinTargetReq("ST-F5", spotC))).Accepted,
            "ST-F5 chained transfer was rejected.");

        await context.SendRefAsync(context.NodeA, actorId, oldRefA, new HandoffPacket("ST-F5", "chain-to-final"));
        await context.WaitEvidenceAsync(context.NodeC, [$"ST-F5|{actorId}|handoff_packet|chain-to-final"]);

        ZlinkStreamAssert.Ensure((await context.JoinAsync(context.NodeC, actorId, new JoinTargetReq("ST-F5", spotB))).Accepted,
            "ST-F5 immediate return transfer to the previous node was rejected.");
        var returnedRef = await context.GetActorRefAsync(context.NodeB, actorId);
        await context.SendRefAsync(context.NodeB, actorId, returnedRef, new HandoffPacket("ST-F5", "returned-within-window"));
        await context.WaitEvidenceAsync(context.NodeB, [$"ST-F5|{actorId}|handoff_packet|returned-within-window"]);

        await Task.Delay(TimeSpan.FromMilliseconds(5300));
        var stale = await context.ProbeRefAsync(context.NodeA, actorId, oldRefA, new ProbeReq("ST-F5", "after-eviction"));
        ZlinkStreamAssert.Ensure(!stale.Succeeded && stale.ErrorKind == "ActorLocationStale",
            $"ST-F5 expected evicted mapping to fail stale, got '{stale.ErrorKind}'.");
        var staleB = await context.ProbeRefAsync(context.NodeB, actorId, oldRefB, new ProbeReq("ST-F5", "after-eviction-b"));
        ZlinkStreamAssert.Ensure(!staleB.Succeeded && staleB.ErrorKind == "ActorLocationStale",
            $"ST-F5 expected node-b mapping eviction, got '{staleB.ErrorKind}'.");
        var evidence = await context.GetEvidenceAsync(context.NodeB);
        SpotActorTransferScenarioContext.RequireNoContains(evidence, $"ST-F5|{actorId}|packet_handler|after-eviction",
            "ST-F5 evicted packet reached the target handler.");
        await context.SendRefAsync(context.NodeB, actorId, returnedRef, new HandoffPacket("ST-F5", "returned-to-b"));
        await context.WaitEvidenceAsync(context.NodeB, [$"ST-F5|{actorId}|handoff_packet|returned-to-b"]);
    }
}
