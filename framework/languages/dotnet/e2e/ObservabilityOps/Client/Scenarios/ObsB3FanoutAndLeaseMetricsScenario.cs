using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using StackExchange.Redis;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsB3FanoutAndLeaseMetricsScenario
{
    private static readonly string[] ForbiddenTags =
        ["correlation_id", "flow_id", "actor_id", "spot_rid"];

    public static async Task RunAsync(ScenarioContext context)
    {
        var beforeA = await EvidenceAsync(context.WorkflowA);
        var beforeB = await EvidenceAsync(context.WorkflowB);
        var suffix = Guid.NewGuid().ToString("N");
        var owner = $"workflow-b3-owner-{suffix}";
        await context.WorkflowA.Post("/workflows").Body(new CreateWorkflowReq(owner)).AsyncRaw();
        await context.WorkflowA.Post("/workflows")
            .Body(new CreateWorkflowReq($"workflow-b3-a-{suffix}", "subscriber")).AsyncRaw();
        await context.WorkflowB.Post("/workflows")
            .Body(new CreateWorkflowReq($"workflow-b3-b-{suffix}", "subscriber")).AsyncRaw();
        await context.WorkflowA.Post($"/workflows/{owner}/advance")
            .Body(new AdvanceWorkflowReq("obs-b3-state")).AsyncRaw();
        await context.WorkflowA.Post($"/workflows/{owner}/publish")
            .Body(new PublishProjectionReq("obs-b3-fanout")).AsyncRaw();
        var marker = $"rid={owner}|version=1|marker=obs-b3-fanout";
        await context.WorkflowA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([marker], [["projection-received|"]])).AsyncRaw();
        await context.WorkflowB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([marker], [["projection-received|"]])).AsyncRaw();

        await using (var redis = await ConnectionMultiplexer.ConnectAsync(context.Options.RedisEndpoint))
        {
            await redis.GetDatabase().ExecuteAsync("CLIENT", "PAUSE", 11000, "ALL");
        }
        // Redis applies the pause after acknowledging CLIENT PAUSE. Waiting outside
        // the service keeps the delay injection external to the framework process.
        await Task.Delay(TimeSpan.FromSeconds(12));

        var afterA = await EvidenceAsync(context.WorkflowA);
        var afterB = await EvidenceAsync(context.WorkflowB);
        var published = Total(afterA.Metrics, "zlink.fanout.published")
                        + Total(afterB.Metrics, "zlink.fanout.published")
                        - Total(beforeA.Metrics, "zlink.fanout.published")
                        - Total(beforeB.Metrics, "zlink.fanout.published");
        var received = Total(afterA.Metrics, "zlink.fanout.received")
                       + Total(afterB.Metrics, "zlink.fanout.received")
                       - Total(beforeA.Metrics, "zlink.fanout.received")
                       - Total(beforeB.Metrics, "zlink.fanout.received");
        ScenarioContext.Require(published == 1 && received == 2,
            $"OBS-B3 expected fanout delta 1:2, got {published}:{received}.");
        var all = afterA.Metrics.Concat(afterB.Metrics).ToArray();
        ScenarioContext.Require(all.All(sample => sample.Name != "zlink.fanout.dropped"),
            "OBS-B3 unobservable fanout backend exposed fanout.dropped.");
        ScenarioContext.Require(all.All(sample => ForbiddenTags.All(tag => !sample.Tags.ContainsKey(tag))),
            "OBS-B3 a metric exposed a forbidden high-cardinality tag.");
        ScenarioContext.Require(all.Any(sample => sample.Name == "zlink.location.owner_lease.renew.lateness"
                                                  && sample.Max >= 0.5m),
            "OBS-B3 external Redis pause did not produce lease renewal lateness.");
        Console.WriteLine("scenario OBS-B3 passed");
    }

    private static decimal Total(IEnumerable<MetricSample> samples, string name) =>
        samples.Where(sample => sample.Name == name).Sum(sample => sample.Value);

    private static async Task<EvidenceSnapshot> EvidenceAsync(Zlink.HttpClient.ZLinkHttpClient client) =>
        (await client.Get("/evidence").Async<EvidenceSnapshot>()).Body;
}
