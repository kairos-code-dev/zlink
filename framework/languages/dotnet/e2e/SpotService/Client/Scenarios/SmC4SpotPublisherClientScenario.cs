using SpotService.Client.Support;
using SpotService.Shared;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmC4SpotPublisherClientScenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, ZLinkHttpClient gateway)
    {
        var spotRid = $"spot-sm-c4-{Guid.NewGuid():N}";
        var created = (await playA.Post("/spot/create")
            .Body(new CreateSpotReq(spotRid))
            .Async<CreateSpotRes>()).Body;
        ScenarioAssert.That(created.SpotRid == spotRid && created.NodeRid == "play-a",
            "SM-C4 publish spot was not created on play-a.");
        var unsubscribedSpotRid = $"spot-sm-c4-unsubscribed-{Guid.NewGuid():N}";
        var unsubscribedSpot = (await playA.Post("/spot/create-alternate")
            .Body(new CreateSpotReq(unsubscribedSpotRid))
            .Async<CreateSpotRes>()).Body;
        ScenarioAssert.That(unsubscribedSpot.SpotRid == unsubscribedSpotRid && unsubscribedSpot.NodeRid == "play-a",
            "SM-C4 unsubscribed spot was not created on play-a.");
        var marker = "sm-c4-publish";
        var waitTask = playA.Post("/spot/publish/wait")
            .Body(new SpotPublishReq(spotRid, marker))
            .Async<SpotPublishObserveRes>();
        var publish = (await gateway.Post("/spot/publish")
            .Body(new SpotPublishReq(spotRid, marker))
            .Async<SpotPublishRes>()).Body;
        var observe = (await waitTask).Body;
        ScenarioAssert.That(publish.Operation == "spot.sm-c4-publish", "SM-C4 publish operation mismatch.");
        ScenarioAssert.That(publish.PublisherRid == "gateway", "SM-C4 publisher was not the publish-only gateway.");
        ScenarioAssert.That(publish.SpotRid == spotRid, "SM-C4 publish target spot mismatch.");
        ScenarioAssert.That(observe.Operation == "spot.sm-c4-observe", "SM-C4 observe operation mismatch.");
        ScenarioAssert.That(observe.Received, "SM-C4 publish-only gateway event was not received.");
        ScenarioAssert.That(
            publish.Evidence.Any(line =>
                line.Contains($"spot-publish|rid=gateway|spot={spotRid}|marker={marker}", StringComparison.Ordinal)),
            "SM-C4 gateway evidence did not include publish marker.");
        ScenarioAssert.That(
            observe.Evidence.Any(line =>
                line.Contains($"spot-msg|rid=play-a|spot={spotRid}|marker={marker}", StringComparison.Ordinal)),
            "SM-C4 evidence did not include spot event publish.");
        ScenarioAssert.That(
            observe.Evidence.All(line =>
                !line.Contains($"spot-msg|rid=play-a|spot={unsubscribedSpotRid}|marker={marker}",
                    StringComparison.Ordinal)),
            "SM-C4 unsubscribed spot received publish event.");
        Console.WriteLine("operation SpotService.sm-c4 passed");
    }
}
