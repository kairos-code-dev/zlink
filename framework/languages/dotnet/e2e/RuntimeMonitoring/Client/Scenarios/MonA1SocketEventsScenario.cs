using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA1SocketEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var service = ZLinkHttpClient.Create(options.ServiceUrl).Build();
        ProfileRes reply;
        await using (var trigger = await MonitoringChannelClient.StartAsync(
                         options, options.ServiceChannelEndpoint, "trigger-mon-a1"))
        {
            reply = await trigger.RequestAsync(new ProfileReq("monitor", "mon-a1-request"));
        }
        ScenarioAssert.That(reply.Value == "profile:monitor", "MON-A1 trigger request failed.");

        var serviceEvidence = await WaitForSocketEvidenceAsync(service);
        ScenarioAssert.That(
            serviceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                        && line.Contains("source=monitor.profile.server", StringComparison.Ordinal)
                                        && (line.Contains("kind=Connected", StringComparison.Ordinal)
                                            || line.Contains("kind=ConnectionReady", StringComparison.Ordinal))),
            "MON-A1 socket connection evidence missing.");
        ScenarioAssert.That(
            serviceEvidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                        && (line.Contains("kind=Disconnected", StringComparison.Ordinal)
                                            || line.Contains("kind=Closed", StringComparison.Ordinal))),
            "MON-A1 socket disconnect evidence missing.");
        Console.WriteLine("scenario MON-A1 passed");
    }

    private static async Task<string[]> WaitForSocketEvidenceAsync(ZLinkHttpClient service)
    {
        var evidence = (await service.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["monitor-socket|", "source=monitor.profile.server"],
                [["kind=Connected", "kind=ConnectionReady"], ["kind=Disconnected", "kind=Closed"]]))
            .SubmitAsync<string[]>()).Body;
        if (evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                 && line.Contains("source=monitor.profile.server", StringComparison.Ordinal)
                                 && (line.Contains("kind=Connected", StringComparison.Ordinal)
                                     || line.Contains("kind=ConnectionReady", StringComparison.Ordinal)))
            && evidence.Any(line => line.Contains("monitor-socket|", StringComparison.Ordinal)
                                    && (line.Contains("kind=Disconnected", StringComparison.Ordinal)
                                        || line.Contains("kind=Closed", StringComparison.Ordinal))))
            return evidence;

        throw new InvalidOperationException("MON-A1 socket connect/disconnect evidence was incomplete.");
    }
}
