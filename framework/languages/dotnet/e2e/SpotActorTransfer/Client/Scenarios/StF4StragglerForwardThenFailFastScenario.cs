// Verifies ST-F4 Straggler Forward Then Fail Fast behavior.
using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StF4StragglerForwardThenFailFastScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var (actorId, oldRef) = await context.TransferForStragglerAsync("ST-F4", 104);
        await context.SendRefAsync(context.NodeA, actorId, oldRef, new HandoffPacket("ST-F4", "G1"));
        await context.WaitEvidenceAsync(context.NodeB, [$"ST-F4|{actorId}|handoff_packet|G1"]);

        await Task.Delay(TimeSpan.FromMilliseconds(5300));
        var stale = await context.ProbeRefAsync(context.NodeA, actorId, oldRef, new ProbeReq("ST-F4", "G2"));
        ZlinkStreamAssert.Ensure(!stale.Succeeded && stale.ErrorKind == "ActorLocationStale",
            $"ST-F4 expected ActorLocationStale, got '{stale.ErrorKind}'.");
    }
}
