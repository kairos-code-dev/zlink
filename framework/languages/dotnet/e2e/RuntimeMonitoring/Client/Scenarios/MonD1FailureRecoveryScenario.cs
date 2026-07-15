// Verifies MON-D1 Failure Recovery behavior.
using System.Diagnostics;
using System.Net.Sockets;
using RuntimeMonitoring.Client.Support;
using RuntimeMonitoring.Shared;
using Zlink.HttpClient;
using Zlink.Framework.E2E.Configuration;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonD1FailureRecoveryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var observer = ZLinkHttpClient.Create(options.ServiceUrl)
            .Timeout(TimeSpan.FromSeconds(35))
            .Build();
        using var serviceB = ZLinkHttpClient.Create(options.ServiceBUrl)
            .Timeout(TimeSpan.FromSeconds(20))
            .Build();
        var baseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;

        await serviceB.Post("/shutdown").Async<object>();
        var serviceBUri = new Uri(options.ServiceBUrl);
        await WaitForPortStateAsync(
            serviceBUri.Host,
            serviceBUri.Port,
            false,
            "MON-D1 expected service-b to stop.");
        await WaitForProcessExitAsync(
            options.ServiceBProcessId,
            "MON-D1 expected the original service-b process to complete framework shutdown.");
        var removed = (await observer.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(
                ["kind=TopologyChanged", "removed=svc-b", "kind=ServiceSummaryChanged",
                    "source=monitor.profile.client", options.ServiceBChannelEndpoint],
                [["kind=Disconnected", "kind=Closed"]],
                TimeoutMilliseconds: 30000,
                AfterIndex: baseline))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(removed.Any(line => line.Contains("removed=svc-b", StringComparison.Ordinal)),
            "MON-D1 did not observe svc-b leaving the topology.");
        var afterRemoved = baseline + removed.Length;

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
            var added = (await observer.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["kind=TopologyChanged", "added=svc-b", "kind=ServiceSummaryChanged",
                        "source=monitor.profile.client", options.ServiceBChannelEndpoint],
                    [["kind=Connected", "kind=ConnectionReady"]],
                    TimeoutMilliseconds: 30000,
                    AfterIndex: afterRemoved))
                .Async<string[]>()).Body;
            ZlinkStreamAssert.Ensure(added.Any(line => line.Contains("added=svc-b", StringComparison.Ordinal)),
                "MON-D1 did not observe svc-b returning to the topology.");

            await observer.Post("/admin/drain").AsyncRaw();
            var reply = (await restartedServiceB.Post("/profile/request")
                .Body(new ProfileReq("restart", "mon-d1-request"))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(
                reply.ProviderRid == "svc-b"
                && reply.Marker == "mon-d1-request"
                && reply.Value == "profile:restart",
                "MON-D1 restarted service did not handle request.");

            var serviceBEvidence = (await restartedServiceB.Post("/evidence/wait")
                .Body(new EvidenceWaitReq(
                    ["profile-request|rid=svc-b|marker=mon-d1-request|value=restart"],
                    []))
                .Async<string[]>()).Body;
            ZlinkStreamAssert.Ensure(
                serviceBEvidence.Any(line => line.Contains(
                    "profile-request|rid=svc-b|marker=mon-d1-request|value=restart",
                    StringComparison.Ordinal)),
                "MON-D1 restarted service evidence missing.");

        }
        finally
        {
            await PostBestEffortAsync(observer, "/admin/restore");
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
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(options.ServiceProject);
        startInfo.ArgumentList.Add("--");
        startInfo.ArgumentList.Add("--config");
        startInfo.ArgumentList.Add(E2eConfiguration.WriteArguments(options.ConfigDir, "svc-b-restart",
        [
            "--role", "service",
            "--rid", "svc-b",
            "--http-url", options.ServiceBUrl,
            "--redis-endpoint", options.RedisEndpoint,
            "--redis-key-prefix", options.RedisKeyPrefix,
            "--channel-endpoint", options.ServiceBChannelEndpoint,
            "--spot-router-endpoint", options.ServiceBSpotRouterEndpoint,
            "--spot-pub-endpoint", options.ServiceBSpotPubEndpoint,
            "--evidence-file", Path.Combine(options.LogDir, "svc-b-restart.evidence.log"),
            "--log-dir", options.LogDir
        ]));

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
            await http.Post(path).Async<object>();
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

    private static async Task WaitForProcessExitAsync(int processId, string failureMessage)
    {
        Process process;
        try
        {
            process = Process.GetProcessById(processId);
        }
        catch (ArgumentException)
        {
            return;
        }

        using (process)
        {
            try
            {
                await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(30));
            }
            catch (TimeoutException exception)
            {
                throw new InvalidOperationException(failureMessage, exception);
            }
        }
    }
}
