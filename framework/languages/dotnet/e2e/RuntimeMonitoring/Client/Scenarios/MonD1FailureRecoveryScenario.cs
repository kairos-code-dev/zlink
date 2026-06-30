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
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Build();
        using var registry = ZLinkHttpClient.Create(options.RegistryUrl).Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl).Build();

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

            var reply = (await trigger.Post("/profile/request/service-b")
                .Body(new ProfileReq("restart", "mon-d1-request"))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(
                reply.ProviderRid == "svc-b"
                && reply.Marker == "mon-d1-request"
                && reply.Value == "profile:restart",
                "MON-D1 restarted service did not handle request.");

            using var restartedServiceB = ZLinkHttpClient.Create(options.ServiceBUrl).Build();
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

            var registryEvidence = (await registry.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["monitor-registry|source=registry"],
                    [["kind=TopologyChanged|topology=5"]]))
                .SubmitAsync<string[]>()).Body;
            ScenarioAssert.That(
                registryEvidence.Count(line => line.Contains("monitor-registry|source=registry|kind=TopologyChanged",
                    StringComparison.Ordinal)) >= 3,
                "MON-D1 registry topology continuity evidence missing.");
        }
        finally
        {
            using var restartedServiceB = ZLinkHttpClient.Create(options.ServiceBUrl).Build();
            await PostBestEffortAsync(restartedServiceB, "/shutdown");
            await restartedService.WaitForExitAsync();
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
        startInfo.ArgumentList.Add("--registry-router-endpoint");
        startInfo.ArgumentList.Add(options.RegistryRouterEndpoint);
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