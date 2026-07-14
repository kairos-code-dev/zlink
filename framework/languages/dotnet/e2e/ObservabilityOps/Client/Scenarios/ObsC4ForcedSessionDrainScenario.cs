using ObservabilityOps.Client.Support;
using ObservabilityOps.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace ObservabilityOps.Client.Scenarios;

internal static class ObsC4ForcedSessionDrainScenario
{
    public static async Task RunAsync(ScenarioContext context)
    {
        var disconnected = new TaskCompletionSource<ZlinkStreamCloseReason>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var closingObserved = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        await using var connector = await context.ConnectAsync(
            reconnectEnabled: false,
            configure: candidate =>
            {
                candidate.Disconnected += (closed, _) =>
                {
                    disconnected.TrySetResult(closed.CloseReason);
                    return ValueTask.CompletedTask;
                };
                candidate.ObserveInbound((frame, _) =>
                {
                    if (frame.Name == "session-closing") closingObserved.TrySetResult();
                    return ValueTask.CompletedTask;
                });
            });
        await connector.Request(new AuthenticateReq($"obs-c4-{Guid.NewGuid():N}"))
            .Async<AuthenticateRes>();
        await context.Session.Post("/drain?deadlineMs=100").SubmitRawAsync();
        await closingObserved.Task.WaitAsync(TimeSpan.FromSeconds(10));
        var reason = await disconnected.Task.WaitAsync(TimeSpan.FromSeconds(10));
        ScenarioContext.Require(reason == ZlinkStreamCloseReason.ServerDrain,
            $"OBS-C4 connector close reason was {reason}, not ServerDrain.");
        var result = await ScenarioContext.WaitForDrainAsync(
            context.Session, TimeSpan.FromSeconds(10));
        ScenarioContext.Require(result.Result == "ForceStopped" && result.Reason == "DeadlineExceeded",
            $"OBS-C4 short deadline returned {result.Result}/{result.Reason}.");
        var metrics = (await context.Session.Get("/evidence").SubmitAsync<EvidenceSnapshot>()).Body.Metrics;
        ScenarioContext.Require(metrics.Any(sample => sample.Name == "zlink.drain.forced"
                                                      && sample.Tags.GetValueOrDefault("kind") == "session"
                                                      && sample.Value >= 1),
            "OBS-C4 forced session metric was not recorded.");
        Console.WriteLine("scenario OBS-C4 passed");
    }

}
