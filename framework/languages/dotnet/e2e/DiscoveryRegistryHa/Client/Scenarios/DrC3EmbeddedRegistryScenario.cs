using System.Diagnostics;
using System.Net.Sockets;
using DiscoveryRegistryHa.Client;
using DiscoveryRegistryHa.Shared;
using Zlink.HttpClient;

namespace DiscoveryRegistryHa.Client.Scenarios;

// DR-C3 verifies an established channel keeps working during a full registry
// outage, then checks recovery after registries restart and a provider re-advertises.
internal static class DrC3EmbeddedRegistryScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var survivorConsumer = CreateClient(options.Reg1ConsumerUrl);
        using var recoveredConsumer = CreateClient(options.Reg2ConsumerUrl);
        using var providerA = CreateClient(options.ProviderAUrl);
        using var providerB = CreateClient(options.ProviderBUrl);
        using var providerC = CreateClient(options.ProviderCUrl);

        var before = await RequestProfileAsync(survivorConsumer, "dr-c3-before");
        ScenarioAssert.That(before.ProviderRid is "api-a" or "api-b", "DR-C3 pre-outage request failed.");
        await WaitForEvidenceAsync(before, before.ProviderRid == "api-a" ? providerA : providerB);

        await StopServerAsync(options.Reg1Url);
        await StopServerAsync(options.Reg2Url);
        await StopServerAsync(options.Reg3Url);

        var during = await RequestProfileAsync(survivorConsumer, "dr-c3-during");
        ScenarioAssert.That(during.ProviderRid is "api-a" or "api-b", "DR-C3 established channel failed during registry outage.");
        await WaitForEvidenceAsync(during, during.ProviderRid == "api-a" ? providerA : providerB);
        await StopServerAsync(options.ProviderAUrl);
        await StopServerAsync(options.ProviderBUrl);

        var started = new List<ManagedProcess>();
        try
        {
            started.Add(await StartRegistryAsync(
                options,
                "reg-1-after-all-outage",
                "reg-1",
                "1",
                options.Reg1Url,
                options.Reg1PubEndpoint,
                options.Reg1RouterEndpoint,
                [options.Reg2PubEndpoint, options.Reg3PubEndpoint]));
            started.Add(await StartRegistryAsync(
                options,
                "reg-2-after-all-outage",
                "reg-2",
                "2",
                options.Reg2Url,
                options.Reg2PubEndpoint,
                options.Reg2RouterEndpoint,
                options.Reg2PeerPubEndpoints));
            started.Add(await StartRegistryAsync(
                options,
                "reg-3-after-all-outage",
                "reg-3",
                "3",
                options.Reg3Url,
                options.Reg3PubEndpoint,
                options.Reg3RouterEndpoint,
                [options.Reg1PubEndpoint, options.Reg2PubEndpoint]));
            started.Add(await StartProviderAsync(options));

            using var reg2 = CreateClient(options.Reg2Url);
            await reg2.Post("/registry/members/wait")
                .Body(new MemberEndpointWaitRequest(options.ProviderCEndpoint))
                .SubmitRawAsync();

            await StopServerAsync(options.Reg2ConsumerUrl);
            await using var recoveredConsumerProcess = await StartConsumerAsync(options);
            using var freshRecoveredConsumer = CreateClient(options.Reg2ConsumerUrl);
            var after = await RequestProfileAsync(freshRecoveredConsumer, "dr-c3-after");
            ScenarioAssert.That(after.ProviderRid == "api-c", "DR-C3 recovered registry did not route to api-c.");
            await WaitForEvidenceAsync(after, providerC);
        }
        finally
        {
            for (var i = started.Count - 1; i >= 0; i--)
            {
                await started[i].DisposeAsync();
            }
        }

        Console.WriteLine("scenario DR-C3 passed");
    }

    static ZLinkHttpClient CreateClient(string baseUrl) =>
        ZLinkHttpClient.Create(baseUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

    static async Task<ProfileReply> RequestProfileAsync(ZLinkHttpClient consumer, string phase)
    {
        var marker = $"{phase}-{Guid.NewGuid():N}";
        var reply = (await consumer.Post("/profile/request")
            .Body(new ProfileRequest("dr-c3", marker))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(reply.Value == "profile:dr-c3", $"DR-C3 {phase} request failed.");
        ScenarioAssert.That(reply.Marker == marker, $"DR-C3 {phase} marker mismatch.");
        return reply;
    }

    static async Task WaitForEvidenceAsync(ProfileReply reply, ZLinkHttpClient provider)
    {
        var evidence = (await provider.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(reply.Marker))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains(reply.Marker, StringComparison.Ordinal)
                && line.Contains($"rid={reply.ProviderRid}", StringComparison.Ordinal)),
            $"DR-C3 evidence was not recorded for {reply.Marker}.");
    }

    static async Task StopServerAsync(string baseUrl)
    {
        try
        {
            using var server = ZLinkHttpClient.Create(baseUrl)
                .Json()
                .Timeout(TimeSpan.FromSeconds(2))
                .Build();
            await server.Post("/shutdown").SubmitAsync<object>();
        }
        catch
        {
        }
    }

    static async Task<ManagedProcess> StartRegistryAsync(
        ClientOptions options,
        string name,
        string rid,
        string registryId,
        string httpUrl,
        string pubEndpoint,
        string routerEndpoint,
        IReadOnlyList<string> peers)
    {
        var args = new List<string>
        {
            "--rid", rid,
            "--registry-id", registryId,
            "--http-url", httpUrl,
            "--registry-pub-endpoint", pubEndpoint,
            "--registry-router-endpoint", routerEndpoint,
            "--log-dir", options.LogDir,
        };
        foreach (var peer in peers)
        {
            args.Add("--peer-pub-endpoint");
            args.Add(peer);
        }

        var process = ManagedProcess.Start(options.RegistryProject, name, rid, options.LogDir, args);
        await process.WaitReadyAsync(httpUrl);
        return process;
    }

    static async Task<ManagedProcess> StartProviderAsync(ClientOptions options)
    {
        var args = new List<string>
        {
            "--rid", "api-c",
            "--http-url", options.ProviderCUrl,
            "--channel-endpoint", options.ProviderCEndpoint,
            "--discovery-endpoint", options.Reg1RouterEndpoint,
            "--discovery-endpoint", options.Reg2RouterEndpoint,
            "--discovery-endpoint", options.Reg3RouterEndpoint,
            "--evidence-file", Path.Combine(options.LogDir, "api-c-after-all-outage.evidence.log"),
            "--log-dir", options.LogDir,
        };
        var process = ManagedProcess.Start(options.ProviderProject, "api-c-after-all-outage", "api-c", options.LogDir, args);
        await process.WaitReadyAsync(options.ProviderCUrl);
        return process;
    }

    static async Task<ManagedProcess> StartConsumerAsync(ClientOptions options)
    {
        var args = new List<string>
        {
            "--rid", "consumer-reg2-recovered",
            "--http-url", options.Reg2ConsumerUrl,
            "--discovery-endpoint", options.Reg2RouterEndpoint,
            "--log-dir", options.LogDir,
        };
        var process = ManagedProcess.Start(
            options.ConsumerProject,
            "consumer-reg2-recovered",
            "consumer-reg2-recovered",
            options.LogDir,
            args);
        await process.WaitReadyAsync(options.Reg2ConsumerUrl);
        return process;
    }

    private sealed class ManagedProcess(Process process, string httpUrl) : IAsyncDisposable
    {
        public static ManagedProcess Start(
            string project,
            string name,
            string rid,
            string logDir,
            IReadOnlyList<string> arguments)
        {
            var startInfo = new ProcessStartInfo("dotnet")
            {
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                UseShellExecute = false,
            };
            startInfo.Environment["ZLINK_E2E_RID"] = rid;
            startInfo.ArgumentList.Add("run");
            startInfo.ArgumentList.Add("--no-build");
            startInfo.ArgumentList.Add("--project");
            startInfo.ArgumentList.Add(project);
            startInfo.ArgumentList.Add("--");
            foreach (var argument in arguments)
            {
                startInfo.ArgumentList.Add(argument);
            }

            var process = new Process { StartInfo = startInfo };
            process.Start();
            _ = CopyToFileAsync(process.StandardOutput, Path.Combine(logDir, $"{name}.stdout.log"));
            _ = CopyToFileAsync(process.StandardError, Path.Combine(logDir, $"{name}.stderr.log"));
            return new ManagedProcess(process, arguments[arguments.ToList().IndexOf("--http-url") + 1]);
        }

        public async Task WaitReadyAsync(string healthUrl)
        {
            var uri = new Uri(healthUrl);
            for (var attempt = 0; attempt < 120; attempt++)
            {
                if (process.HasExited)
                {
                    throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");
                }

                if (await CanConnectAsync(uri.Host, uri.Port))
                {
                    return;
                }

                await Task.Delay(TimeSpan.FromMilliseconds(250));
            }

            throw new TimeoutException($"Timed out waiting for {healthUrl}.");
        }

        static async Task<bool> CanConnectAsync(string host, int port)
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

        public async ValueTask DisposeAsync()
        {
            if (process.HasExited)
            {
                return;
            }

            await StopServerAsync(httpUrl);
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            try
            {
                await process.WaitForExitAsync(timeout.Token);
            }
            catch (OperationCanceledException)
            {
                if (!process.HasExited)
                {
                    process.Kill(entireProcessTree: true);
                    await process.WaitForExitAsync();
                }
            }
        }

        private static async Task CopyToFileAsync(StreamReader reader, string path)
        {
            await using var stream = File.Open(path, FileMode.Create, FileAccess.Write, FileShare.Read);
            await using var writer = new StreamWriter(stream);
            while (await reader.ReadLineAsync() is { } line)
            {
                await writer.WriteLineAsync(line);
                await writer.FlushAsync();
            }
        }
    }
}
