using System.Diagnostics;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client;

internal sealed class ResilienceProcessManager(ClientOptions options) : IAsyncDisposable
{
    readonly List<ManagedProcess> _processes = [];

    public async Task<ProviderStartResult> StartProviderAAsync()
    {
        var process = StartProvider(
            "api-a-restart",
            "api-a",
            options.ProviderAUrl,
            options.ProviderAEndpoint,
            options.ProviderAEvidenceFile);
        await process.WaitReadyAsync();
        return new ProviderStartResult("api-a", "started", options.ProviderAUrl, options.ProviderAEndpoint);
    }

    public async Task<ProviderStartResult> StartProviderBAsync()
    {
        var process = StartProvider(
            "api-b-restart",
            "api-b",
            options.ProviderBUrl,
            options.ProviderBEndpoint,
            options.ProviderBEvidenceFile);
        await process.WaitReadyAsync();
        return new ProviderStartResult("api-b", "started", options.ProviderBUrl, options.ProviderBEndpoint);
    }

    public async Task<ProviderStartResult> StartProviderBRemapAsync()
    {
        var process = StartProvider(
            "api-b-rescheduled",
            "api-b",
            options.ProviderBRemapUrl,
            options.ProviderBRemapEndpoint,
            Path.Combine(options.LogDir, "api-b-rescheduled.evidence.log"));
        await process.WaitReadyAsync();
        return new ProviderStartResult("api-b", "started", options.ProviderBRemapUrl, options.ProviderBRemapEndpoint);
    }

    public async Task<ProviderStartResult> StartProviderBGreenAsync()
    {
        var process = StartProvider(
            "api-b-green",
            "api-b",
            options.ProviderBGreenUrl,
            options.ProviderBGreenEndpoint,
            Path.Combine(options.LogDir, "api-b-green.evidence.log"));
        await process.WaitReadyAsync();
        return new ProviderStartResult("api-b", "started", options.ProviderBGreenUrl, options.ProviderBGreenEndpoint);
    }

    public async Task StartRegistryAsync()
    {
        var process = StartProcess(
            "registry-restart",
            "registry",
            options.RegistryProject,
            [
                "--rid", "registry",
                "--http-url", options.RegistryUrl,
                "--registry-pub-endpoint", options.RegistryPubEndpoint,
                "--registry-router-endpoint", options.RegistryRouterEndpoint,
                "--log-dir", options.LogDir,
            ],
            options.RegistryUrl);
        await process.WaitReadyAsync();
    }

    public async Task WaitRegistryHealthAsync(bool expectedHealthy, TimeSpan timeout)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (await IsHealthyAsync(options.RegistryUrl) == expectedHealthy)
            {
                return;
            }

            await Task.Delay(250);
        }

        throw new TimeoutException($"Registry health did not become {expectedHealthy}.");
    }

    public async ValueTask DisposeAsync()
    {
        for (var i = _processes.Count - 1; i >= 0; i--)
        {
            await _processes[i].StopAsync();
        }

        _processes.Clear();
    }

    ManagedProcess StartProvider(
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
                "--registry-router-endpoint", options.RegistryRouterEndpoint,
                "--channel-endpoint", endpoint,
                "--evidence-file", evidenceFile,
                "--log-dir", options.LogDir,
            ],
            url);
    }

    ManagedProcess StartProcess(
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
            UseShellExecute = false,
        };
        startInfo.Environment["ZLINK_E2E_RID"] = rid;
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(project);
        startInfo.ArgumentList.Add("--");
        foreach (var argument in arguments)
        {
            startInfo.ArgumentList.Add(argument);
        }

        var process = Process.Start(startInfo)
            ?? throw new InvalidOperationException($"Failed to start {name}.");
        _ = CopyToFileAsync(process.StandardOutput, Path.Combine(options.LogDir, $"{name}.stdout.log"));
        _ = CopyToFileAsync(process.StandardError, Path.Combine(options.LogDir, $"{name}.stderr.log"));
        var managed = new ManagedProcess(process, healthUrl);
        _processes.Add(managed);
        return managed;
    }

    static async Task<bool> IsHealthyAsync(string url)
    {
        try
        {
            using var http = ZLinkHttpClient.Create(url).Json().Timeout(TimeSpan.FromSeconds(2)).Build();
            return (await http.Get("/health").SubmitRawAsync()).Status == 200;
        }
        catch
        {
            return false;
        }
    }

    static async Task CopyToFileAsync(StreamReader reader, string path)
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

internal sealed record ProviderStartResult(string Rid, string Status, string Url, string Endpoint);

internal sealed class ManagedProcess(Process process, string healthUrl)
{
    bool _disposed;

    public async Task WaitReadyAsync()
    {
        for (var i = 0; i < 120; i++)
        {
            if (process.HasExited)
            {
                throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");
            }

            try
            {
                using var http = ZLinkHttpClient.Create(healthUrl).Json().Timeout(TimeSpan.FromSeconds(2)).Build();
                if ((await http.Get("/health").SubmitRawAsync()).Status == 200)
                {
                    return;
                }
            }
            catch
            {
            }

            await Task.Delay(250);
        }

        throw new TimeoutException($"Process did not become ready: {healthUrl}.");
    }

    public async Task StopAsync()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        if (!process.HasExited)
        {
            try
            {
                using var http = ZLinkHttpClient.Create(healthUrl).Json().Timeout(TimeSpan.FromSeconds(5)).Build();
                await http.Post("/shutdown").SubmitRawAsync();
            }
            catch
            {
                if (!process.HasExited)
                {
                    process.Kill(entireProcessTree: true);
                }
            }
        }

        await process.WaitForExitAsync();
        process.Dispose();
    }
}
