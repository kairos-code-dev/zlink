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
        var serviceBUri = new Uri(options.ServiceBUrl);
        var serviceBChannelUri = new Uri(options.ServiceBChannelEndpoint);
        Process? current = Process.GetProcessById(options.ServiceBProcessId);
        try
        {
            for (var cycle = 1; cycle <= 3; cycle++)
            {
                var downBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
                current.Kill(entireProcessTree: true);
                await current.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
                current.Dispose();
                current = null;
                await WaitForPortStateAsync(
                    serviceBUri.Host, serviceBUri.Port, false,
                    $"MON-D1 cycle {cycle} expected service-b HTTP port to close after SIGKILL.");
                var removed = (await observer.Post("/evidence/wait")
                    .Body(new EvidenceWaitReq(
                        ["kind=TopologyChanged", "removed=svc-b", "kind=ServiceSummaryChanged",
                            "source=monitor.profile", options.ServiceBChannelEndpoint],
                        [["kind=Disconnected", "kind=Closed"]],
                        TimeoutMilliseconds: 30000,
                        AfterIndex: downBaseline))
                    .Timeout(TimeSpan.FromSeconds(35))
                    .Async<string[]>()).Body;
                ZlinkStreamAssert.Ensure(
                    removed.Any(line => line.Contains("removed=svc-b", StringComparison.Ordinal)),
                    $"MON-D1 cycle {cycle} did not observe svc-b leaving after lease expiry.");

                var upBaseline = (await observer.Get("/evidence").Async<string[]>()).Body.Length;
                current = StartServiceB(options, cycle);
                await WaitForPortStateAsync(
                    serviceBUri.Host, serviceBUri.Port, true,
                    $"MON-D1 cycle {cycle} expected service-b to restart.");
                await WaitForPortStateAsync(
                    serviceBChannelUri.Host, serviceBChannelUri.Port, true,
                    $"MON-D1 cycle {cycle} expected service-b channel endpoint to restart.");
                var added = (await observer.Post("/evidence/wait")
                    .Body(new EvidenceWaitReq(
                        ["kind=TopologyChanged", "added=svc-b", "kind=ServiceSummaryChanged",
                            "source=monitor.profile", options.ServiceBChannelEndpoint],
                        [["kind=Connected", "kind=ConnectionReady"]],
                        TimeoutMilliseconds: 30000,
                        AfterIndex: upBaseline))
                    .Timeout(TimeSpan.FromSeconds(35))
                    .Async<string[]>()).Body;
                ZlinkStreamAssert.Ensure(
                    added.Any(line => line.Contains("added=svc-b", StringComparison.Ordinal)),
                    $"MON-D1 cycle {cycle} did not observe svc-b returning with a new owner generation.");
            }

            // Keep the weight transition outside the crash cycles.
            await observer.Post("/admin/weight/exclude").AsyncRaw();
            using var activeServiceB = ZLinkHttpClient.Create(options.ServiceBUrl)
                .Timeout(TimeSpan.FromSeconds(35))
                .Build();
            var reply = (await activeServiceB.Post("/profile/request")
                .Body(new ProfileReq("restart", "mon-d1-request"))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(
                reply.ProviderRid == "svc-b"
                && reply.Marker == "mon-d1-request"
                && reply.Value == "profile:restart",
                "MON-D1 restarted service did not handle request.");

            var serviceBEvidence = (await activeServiceB.Post("/evidence/wait")
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
            await PostBestEffortAsync(observer, "/admin/weight/include");
            using var activeServiceB = ZLinkHttpClient.Create(options.ServiceBUrl)
                .Timeout(TimeSpan.FromSeconds(20))
                .Build();
            await PostBestEffortAsync(activeServiceB, "/shutdown");
            if (current is not null)
            {
                try
                {
                    await current.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
                }
                catch (TimeoutException)
                {
                    if (!current.HasExited) current.Kill(true);
                    await current.WaitForExitAsync();
                }
                current.Dispose();
            }
        }

        Console.WriteLine("scenario MON-D1 passed");
    }

    private static Process StartServiceB(ClientOptions options, int cycle)
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
        startInfo.ArgumentList.Add("--no-build");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(options.ServiceProject);
        startInfo.ArgumentList.Add("--");
        startInfo.ArgumentList.Add("--config");
        startInfo.ArgumentList.Add(E2eConfiguration.Write(
            options.ConfigDir,
            $"svc-b-restart-{cycle}",
            new RestartServiceOptions(
                "service",
                options.ServiceBUrl,
                options.LogDir,
                "svc-b",
                Path.Combine(options.LogDir, $"svc-b-restart-{cycle}.evidence.log"),
                options.RedisEndpoint,
                options.RedisKeyPrefix,
                options.ServiceBChannelEndpoint,
                options.ServiceBSpotRouterEndpoint,
                options.ServiceBSpotPubEndpoint)));

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
        catch (Zlink.Framework.Contracts.Errors.ZLinkFrameworkException)
        {
        }
    }

    private static async Task WaitForPortStateAsync(string host, int port, bool shouldBeOpen, string failureMessage)
    {
        for (var attempt = 0; attempt < 30; attempt++)
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

internal sealed record RestartServiceOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string Rid,
    string EvidenceFile,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string ChannelEndpoint,
    string SpotRouterEndpoint,
    string SpotPubEndpoint);
