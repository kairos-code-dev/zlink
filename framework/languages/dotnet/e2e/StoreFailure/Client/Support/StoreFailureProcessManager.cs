using System.Diagnostics;
using Zlink.HttpClient;

namespace StoreFailure.Client.Support;

internal sealed class StoreFailureProcessManager(ClientOptions options) : IAsyncDisposable
{
    private readonly List<ManagedProcess> _processes = [];

    public async ValueTask DisposeAsync()
    {
        for (var i = _processes.Count - 1; i >= 0; i--) await _processes[i].StopAsync();

        _processes.Clear();
    }

    public async Task<ManagedProcess> StartProviderBAsync()
    {
        var process = StartProvider(
            "api-b",
            "api-b",
            options.ProviderBUrl,
            options.ProviderBEndpoint,
            options.ProviderBEvidenceFile);
        await process.WaitReadyAsync();
        return process;
    }

    public async Task<ManagedProcess> StartProviderCAsync()
    {
        var process = StartProvider(
            "api-c",
            "api-c",
            options.ProviderCUrl,
            options.ProviderCEndpoint,
            Path.Combine(options.LogDir, "api-c.evidence.log"));
        await process.WaitReadyAsync();
        return process;
    }

    /// <summary>
    /// The polling-only consumer observes the store without the
    /// change-stamp optimization (SF-A2).
    /// </summary>
    public async Task<ManagedProcess> StartConsumerNwAsync()
    {
        var process = StartProcess(
            "consumer-nw",
            "consumer-nw",
            options.ConsumerProject,
            [
                "--http-url", options.ConsumerNwUrl,
                "--redis-endpoint", options.RedisEndpoint,
                "--redis-key-prefix", options.RedisKeyPrefix,
                "--store-mode", "polling",
                "--trace-label", "consumer-nw",
                "--location-heartbeat-ms", options.LocationHeartbeatMs.ToString(),
                "--location-lease-ttl-ms", options.LocationLeaseTtlMs.ToString(),
                "--location-polling-ms", options.LocationPollingMs.ToString(),
                "--location-grace-ms", options.LocationGraceMs.ToString(),
                "--log-dir", options.LogDir
            ],
            options.ConsumerNwUrl);
        await process.WaitReadyAsync();
        return process;
    }

    /// <summary>Pauses the store container: outage begins.</summary>
    public Task PauseStoreAsync() => RunDockerAsync("pause", options.RedisContainer);

    public Task UnpauseStoreAsync() => RunDockerAsync("unpause", options.RedisContainer);

    private static async Task RunDockerAsync(string verb, string container)
    {
        var startInfo = new ProcessStartInfo("docker")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add(verb);
        startInfo.ArgumentList.Add(container);
        using var process = Process.Start(startInfo)
                            ?? throw new InvalidOperationException($"Failed to run docker {verb}.");
        await process.WaitForExitAsync();
        if (process.ExitCode != 0)
        {
            var error = await process.StandardError.ReadToEndAsync();
            throw new InvalidOperationException($"docker {verb} {container} failed: {error}");
        }
    }

    private ManagedProcess StartProvider(
        string name,
        string rid,
        string url,
        string endpoint,
        string evidenceFile)
    {
        return StartProcess(
            name,
            rid,
            options.ProviderProject,
            [
                "--rid", rid,
                "--http-url", url,
                "--redis-endpoint", options.RedisEndpoint,
                "--redis-key-prefix", options.RedisKeyPrefix,
                "--channel-endpoint", endpoint,
                "--evidence-file", evidenceFile,
                "--location-heartbeat-ms", options.LocationHeartbeatMs.ToString(),
                "--location-lease-ttl-ms", options.LocationLeaseTtlMs.ToString(),
                "--location-polling-ms", options.LocationPollingMs.ToString(),
                "--location-grace-ms", options.LocationGraceMs.ToString(),
                "--log-dir", options.LogDir
            ],
            url);
    }

    private ManagedProcess StartProcess(
        string name,
        string rid,
        string project,
        IReadOnlyList<string> arguments,
        string healthUrl)
    {
        var startInfo = new ProcessStartInfo("dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.Environment["ZLINK_E2E_RID"] = rid;
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--no-build");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(project);
        startInfo.ArgumentList.Add("--");
        foreach (var argument in arguments) startInfo.ArgumentList.Add(argument);

        var process = Process.Start(startInfo)
                      ?? throw new InvalidOperationException($"Failed to start {name}.");
        _ = CopyToFileAsync(process.StandardOutput, Path.Combine(options.LogDir, $"{name}.stdout.log"));
        _ = CopyToFileAsync(process.StandardError, Path.Combine(options.LogDir, $"{name}.stderr.log"));
        var managed = new ManagedProcess(process, healthUrl);
        _processes.Add(managed);
        return managed;
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

internal sealed class ManagedProcess(Process process, string healthUrl)
{
    private bool _stopped;

    public async Task WaitReadyAsync()
    {
        for (var i = 0; i < 120; i++)
        {
            if (process.HasExited)
                throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");

            try
            {
                using var http = ZLinkHttpClient.Create(healthUrl).Timeout(TimeSpan.FromSeconds(2)).Build();
                if ((await http.Get("/health").SubmitRawAsync()).Status == 200) return;
            }
            catch
            {
            }

            await Task.Delay(250);
        }

        throw new TimeoutException($"Process did not become ready: {healthUrl}.");
    }

    /// <summary>
    /// SIGKILL, process tree included: the crash path that leaves rows
    /// behind with no shutdown cleanup (SF-C1, SF-D2).
    /// </summary>
    public async Task KillAsync()
    {
        _stopped = true;
        if (!process.HasExited) process.Kill(true);

        await process.WaitForExitAsync();
    }

    public async Task StopAsync()
    {
        if (_stopped) return;

        _stopped = true;
        if (!process.HasExited)
            try
            {
                using var http = ZLinkHttpClient.Create(healthUrl).Timeout(TimeSpan.FromSeconds(5)).Build();
                await http.Post("/shutdown").SubmitRawAsync();
            }
            catch
            {
                if (!process.HasExited) process.Kill(true);
            }

        using var exitWait = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        try
        {
            await process.WaitForExitAsync(exitWait.Token);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited) process.Kill(true);
        }
    }
}
