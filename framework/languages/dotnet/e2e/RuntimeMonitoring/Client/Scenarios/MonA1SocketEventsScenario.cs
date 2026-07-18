// Verifies socket connect, ready, and disconnect events with endpoint and routing identity payloads.
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA1SocketEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        var baseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
        var service = await EphemeralService.StartAsync(options, "svc-a1");
        var connected = await WaitAsync(
            observer,
            ["source=monitor.profile", service.ChannelEndpoint, "routing=svc-a1"],
            [["kind=ConnectionReady"]],
            baseline);
        ZlinkStreamAssert.Ensure(connected.Any(line =>
                IsMeshEvent(line, service.ChannelEndpoint, "svc-a1")
                && line.Contains("kind=ConnectionReady", StringComparison.Ordinal)),
            "MON-A1 mesh connection identity evidence missing.");

        var disconnectBaseline = baseline + connected.Length;
        await service.DisposeAsync();
        var disconnected = await WaitAsync(
            observer,
            ["source=monitor.profile", service.ChannelEndpoint],
            [["kind=Disconnected"]],
            disconnectBaseline);
        ZlinkStreamAssert.Ensure(disconnected.Any(line =>
                line.Contains("source=monitor.profile", StringComparison.Ordinal)
                && line.Contains($"remote={service.ChannelEndpoint}", StringComparison.Ordinal)
                && line.Contains("kind=Disconnected", StringComparison.Ordinal)),
            "MON-A1 mesh disconnect endpoint evidence missing.");
        Console.WriteLine("scenario MON-A1 passed");
    }

    private static bool IsMeshEvent(string line, string endpoint, string rid)
        => line.Contains("monitor-mesh|", StringComparison.Ordinal)
           && line.Contains("source=monitor.profile", StringComparison.Ordinal)
           && line.Contains($"remote={endpoint}", StringComparison.Ordinal)
           && line.Contains($"routing={rid}", StringComparison.Ordinal);

    private static async Task<string[]> WaitAsync(
        ZLinkHttpClient observer,
        string[] all,
        string[][] any,
        int afterIndex)
        => (await observer.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(all, any, TimeoutMilliseconds: 30000, AfterIndex: afterIndex))
            .Async<string[]>()).Body;
}
