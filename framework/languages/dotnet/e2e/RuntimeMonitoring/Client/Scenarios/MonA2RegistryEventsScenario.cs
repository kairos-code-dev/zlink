using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonA2RegistryEventsScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(30))
            .Build();
        var baseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
        var httpPort = ReservePort();
        var channelPort = ReservePort();
        var serviceUrl = $"http://127.0.0.1:{httpPort}";
        var channelEndpoint = $"tcp://127.0.0.1:{channelPort}";
        using var service = StartService(options, serviceUrl, channelEndpoint);

        try
        {
            await WaitForHealthAsync(serviceUrl);
            var added = (await observer.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["kind=TopologyChanged", "added=svc-c"],
                    [],
                    AfterIndex: baseline))
                .Async<string[]>()).Body;
            ScenarioAssert.That(added.Any(line => line.Contains("added=svc-c", StringComparison.Ordinal)),
                "MON-A2 did not observe the added service identity.");

            var afterAdded = baseline + added.Length;
            using var serviceHttp = ZLinkHttpClient.Create(serviceUrl).Build();
            await serviceHttp.Post("/shutdown").AsyncRaw();
            await service.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));

            var removed = (await observer.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["kind=TopologyChanged", "removed=svc-c"],
                    [],
                    AfterIndex: afterAdded,
                    TimeoutMilliseconds: 30000))
                .Async<string[]>()).Body;
            ScenarioAssert.That(removed.Any(line => line.Contains("removed=svc-c", StringComparison.Ordinal)),
                "MON-A2 did not observe the removed service identity.");
        }
        finally
        {
            if (!service.HasExited) service.Kill(true);
            await service.WaitForExitAsync();
        }

        Console.WriteLine("scenario MON-A2 passed");
    }

    private static Process StartService(
        ClientOptions options,
        string serviceUrl,
        string channelEndpoint)
    {
        var start = new ProcessStartInfo("dotnet") { UseShellExecute = false };
        start.ArgumentList.Add("run");
        start.ArgumentList.Add("--no-build");
        start.ArgumentList.Add("--project");
        start.ArgumentList.Add(options.FilteredServiceProject);
        start.ArgumentList.Add("--");
        Add(start, "--rid", "svc-c");
        Add(start, "--http-url", serviceUrl);
        Add(start, "--redis-endpoint", options.RedisEndpoint);
        Add(start, "--redis-key-prefix", options.RedisKeyPrefix);
        Add(start, "--channel-endpoint", channelEndpoint);
        Add(start, "--evidence-file", Path.Combine(options.LogDir, "svc-c.evidence.log"));
        Add(start, "--log-dir", options.LogDir);
        return Process.Start(start)
               ?? throw new InvalidOperationException("Failed to start MON-A2 service-c.");
    }

    private static void Add(ProcessStartInfo start, string name, string value)
    {
        start.ArgumentList.Add(name);
        start.ArgumentList.Add(value);
    }

    private static int ReservePort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }

    private static async Task WaitForHealthAsync(string url)
    {
        using var http = ZLinkHttpClient.Create(url)
            .Timeout(TimeSpan.FromMilliseconds(500))
            .Build();
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(10);
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                if ((await http.Get("/health").AsyncRaw()).Status == 200) return;
            }
            catch
            {
            }
            await Task.Delay(50);
        }
        throw new TimeoutException("MON-A2 service-c did not become ready.");
    }
}
