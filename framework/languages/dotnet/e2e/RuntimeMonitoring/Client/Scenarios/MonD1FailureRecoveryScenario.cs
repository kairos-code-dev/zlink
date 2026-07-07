using System.Diagnostics;
using System.Net.Sockets;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonD1FailureRecoveryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl)
            .Timeout(TimeSpan.FromSeconds(20))
            .Build();
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl)
            .Timeout(TimeSpan.FromSeconds(20))
            .Build();

        await serviceB.Post("/shutdown").SubmitAsync<object>();
        var serviceBUri = new Uri(options.ServiceBUrl);
        await WaitForPortStateAsync(
            serviceBUri.Host,
            serviceBUri.Port,
            false,
            "MON-D1 expected service-b to stop.");

        using var restartedService = StartServiceB(options);
        try
        {
            await WaitForPortStateAsync(
                serviceBUri.Host,
                serviceBUri.Port,
                true,
                "MON-D1 expected service-b to restart.");

            var serviceBChannelUri = new Uri(options.ServiceBChannelEndpoint);
            await WaitForPortStateAsync(
                serviceBChannelUri.Host,
                serviceBChannelUri.Port,
                true,
                "MON-D1 expected service-b channel endpoint to restart.");

            using var restartedServiceB = ZLinkHttpClient.Create(options.ServiceBUrl)
                .Timeout(TimeSpan.FromSeconds(35))
                .Build();
            var reply = (await trigger.Post("/profile/request/service-b")
                .Body(new ProfileReq("restart", "mon-d1-request"))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(
                reply.ProviderRid == "svc-b"
                && reply.Marker == "mon-d1-request"
                && reply.Value == "profile:restart",
                "MON-D1 restarted service did not handle request.");

            var serviceBEvidence = (await restartedServiceB.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["profile-request|rid=svc-b|marker=mon-d1-request|value=restart"],
                    []))
                .SubmitAsync<string[]>()).Body;
            ScenarioAssert.That(
                serviceBEvidence.Any(line => line.Contains(
                    "profile-request|rid=svc-b|marker=mon-d1-request|value=restart",
                    StringComparison.Ordinal)),
                "MON-D1 restarted service evidence missing.");

            // The observer's own projection must have seen the full
            // remove/re-add cycle: initial convergence, svc-b leaving,
            // and svc-b returning are at least three TopologyChanged
            // emissions.
            var continuitySeen = false;
            for (var attempt = 0; attempt < 60 && !continuitySeen; attempt++)
            {
                var observerEvidence = (await observer.Post("/evidence/wait")
                    .Body(new EvidenceWaitReq(
                        ["monitor-location-runtime|source=location-runtime"],
                        [["kind=TopologyChanged"]]))
                    .SubmitAsync<string[]>()).Body;
                continuitySeen = observerEvidence.Count(line => line.Contains(
                    "monitor-location-runtime|source=location-runtime|kind=TopologyChanged",
                    StringComparison.Ordinal)) >= 3;
                if (!continuitySeen) await Task.Delay(250);
            }

            ScenarioAssert.That(continuitySeen, "MON-D1 location runtime topology continuity evidence missing.");
        }
        finally
        {
            using var restartedServiceB = ZLinkHttpClient.Create(options.ServiceBUrl)
                .Timeout(TimeSpan.FromSeconds(20))
                .Build();
            await PostBestEffortAsync(restartedServiceB, "/shutdown");
            try
            {
                await restartedService.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
            }
            catch (TimeoutException)
            {
                if (!restartedService.HasExited) restartedService.Kill(true);
                await restartedService.WaitForExitAsync();
            }
        }

        Console.WriteLine("scenario MON-D1 passed");
    }

    private static Process StartServiceB(ClientOptions options)
    {
        var stdout = Path.Combine(options.LogDir, "svc-b-restart.stdout.log");
        var stderr = Path.Combine(options.LogDir, "svc-b-restart.stderr.log");
        var startInfo = new ProcessStartInfo("dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.Environment["ZLINK_E2E_RID"] = "svc-b";
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(options.FilteredServiceProject);
        startInfo.ArgumentList.Add("--");
        startInfo.ArgumentList.Add("--rid");
        startInfo.ArgumentList.Add("svc-b");
        startInfo.ArgumentList.Add("--http-url");
        startInfo.ArgumentList.Add(options.ServiceBUrl);
        startInfo.ArgumentList.Add("--redis-endpoint");
        startInfo.ArgumentList.Add(options.RedisEndpoint);
        startInfo.ArgumentList.Add("--redis-key-prefix");
        startInfo.ArgumentList.Add(options.RedisKeyPrefix);
        startInfo.ArgumentList.Add("--channel-endpoint");
        startInfo.ArgumentList.Add(options.ServiceBChannelEndpoint);
        startInfo.ArgumentList.Add("--evidence-file");
        startInfo.ArgumentList.Add(Path.Combine(options.LogDir, "svc-b-restart.evidence.log"));
        startInfo.ArgumentList.Add("--log-dir");
        startInfo.ArgumentList.Add(options.LogDir);

        var process = Process.Start(startInfo)
                      ?? throw new InvalidOperationException("Failed to restart service-b.");
        _ = Task.Run(async () => await File.WriteAllTextAsync(stdout, await process.StandardOutput.ReadToEndAsync()));
        _ = Task.Run(async () => await File.WriteAllTextAsync(stderr, await process.StandardError.ReadToEndAsync()));
        return process;
    }

    private static async Task PostBestEffortAsync(ZLinkHttpClient http, string path)
    {
        try
        {
            await http.Post(path).SubmitAsync<object>();
        }
        catch (HttpRequestException)
        {
        }
        catch (TaskCanceledException)
        {
        }
    }

    private static async Task WaitForPortStateAsync(string host, int port, bool shouldBeOpen, string failureMessage)
    {
        for (var attempt = 0; attempt < 100; attempt++)
        {
            if (await CanConnectAsync(host, port) == shouldBeOpen) return;

            await Task.Delay(100);
        }

        throw new InvalidOperationException(failureMessage);
    }

    private static async Task<bool> CanConnectAsync(string host, int port)
    {
        try
        {
            using var client = new TcpClient();
            await client.ConnectAsync(host, port).WaitAsync(TimeSpan.FromMilliseconds(200));
            return true;
        }
        catch (SocketException)
        {
            return false;
        }
        catch (TimeoutException)
        {
            return false;
        }
    }
}
