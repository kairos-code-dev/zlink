using SpotActorTransfer.Client.Support;
using SpotActorTransfer.Shared;
using Zlink.HttpClient;

namespace SpotActorTransfer.Client.Scenarios;

internal static class StC1SourceDownBeforeCommitScenario
{
    public static async Task RunAsync(SpotActorTransferScenarioContext context)
    {
        var actorId = $"actor-source-down-before-commit-{Guid.NewGuid():N}";
        var spotRid = $"spot-source-down-before-commit-{Guid.NewGuid():N}";
        await context.CreateSpotAsync(context.NodeB, spotRid);
        await context.CreateActorAsync(context.NodeA, actorId, SpotActorTransferNames.ActorTypeStateful, 62);

        var joinTask = context.JoinRawAsync(context.NodeA, actorId, new JoinTargetReq("ST-C1", spotRid));
        await context.WaitEvidenceAsync(context.NodeB, [
            $"ST-C1|{actorId}|admission|spot={spotRid}"
        ]);
        await context.WaitEvidenceAsync(context.NodeA, [
            $"transfer|{actorId}|transfer_out|62",
            $"ST-C1|{actorId}|before_commit_gate|62"
        ]);

        await context.ShutdownAsync(context.NodeA);
        try
        {
            var response = await joinTask.WaitAsync(TimeSpan.FromSeconds(3));
            ZlinkStreamAssert.Ensure(!response.Accepted, "ST-C1 join should not be accepted after source shutdown before commit.");
        }
        catch (Exception ex) when (ex is TimeoutException or InvalidOperationException or HttpRequestException)
        {
            // Source shutdown may abort the HTTP request before the app endpoint can return a failure body.
        }

        await Task.Delay(TimeSpan.FromSeconds(5));
        var targetEvidence = await context.GetEvidenceAsync(context.NodeB);
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"transfer|{actorId}|transfer_in|62", "ST-C1 target should not transfer in without commit.");
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"transfer|{actorId}|joined|{spotRid}", "ST-C1 target should not join without commit.");
        SpotActorTransferScenarioContext.RequireNoContains(targetEvidence, $"ST-C1|{actorId}|packet_handler|", "ST-C1 target should not dispatch actor packets without commit.");
    }
}
