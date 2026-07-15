using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Zlink.HttpClient;
using Zlink.Framework.E2E.Configuration;

namespace LocationMessaging.Client.Support;

internal sealed class DynamicClusterLauncher(
    string providerProject,
    string consumerProject,
    string configDir,
    string logDir) : IAsyncDisposable
{
    private readonly List<DynamicProcess> _processes = [];

    public string RedisEndpoint { get; private set; } = "";

    public string RedisKeyPrefix { get; private set; } = "";

    public async ValueTask DisposeAsync()
    {
        for (var i = _processes.Count - 1; i >= 0; i--) await _processes[i].StopAsync();

        _processes.Clear();
    }

    public static Task<DynamicClusterLauncher> StartAsync(ClientOptions options, string scenarioName)
    {
        // No registry process exists. Each dynamic cluster shares the run's
        // Redis instance but isolates its peer location rows under a
        // scenario-specific key prefix (mirrors the doc's per-run isolation).
        var launcher = new DynamicClusterLauncher(
            options.ProviderProject,
            options.ConsumerProject,
            options.ConfigDir,
            options.LogDir)
        {
            RedisEndpoint = options.RedisEndpoint,
            RedisKeyPrefix = $"{options.RedisKeyPrefix}:{scenarioName}"
        };
        return Task.FromResult(launcher);
    }

    public async Task<DynamicProvider> StartProviderAsync(string name, string rid, int weight = 100)
    {
        var httpUrl = PickHttpUrl();
        var channelEndpoint = PickEndpoint();
        var process = StartServer(
            name,
            providerProject,
            [
                "--role", "provider",
                "--rid", rid,
                "--http-url", httpUrl,
                "--redis-endpoint", RedisEndpoint,
                "--redis-key-prefix", RedisKeyPrefix,
                "--channel-endpoint", channelEndpoint,
                "--max-message-size", "2097152",
                "--route-endpoint", PickEndpoint(),
                "--weight", weight.ToString(),
                "--evidence-file", Path.Combine(logDir, $"{name}.evidence.log"),
                "--log-dir", logDir
            ],
            httpUrl,
            channelEndpoint);
        await process.WaitReadyAsync();
        return new DynamicProvider(process, httpUrl, process.RequireChannelEndpoint());
    }

    public async Task<DynamicConsumer> StartConsumerAsync(string name)
    {
        var httpUrl = PickHttpUrl();
        var process = StartServer(
            name,
            consumerProject,
            [
                "--http-url", httpUrl,
                "--redis-endpoint", RedisEndpoint,
                "--redis-key-prefix", RedisKeyPrefix,
                "--trace-label", name,
                "--log-dir", logDir
            ],
            httpUrl,
            channelEndpoint: null);
        await process.WaitReadyAsync();
        return new DynamicConsumer(process, httpUrl);
    }

    public async Task StopAsync(DynamicProvider provider)
    {
        await provider.Process.StopAsync();
        _processes.Remove(provider.Process);
    }

    private DynamicProcess StartServer(
        string name,
        string projectPath,
        IReadOnlyList<string> arguments,
        string httpUrl,
        string? channelEndpoint)
    {
        var startInfo = new ProcessStartInfo
        {
            FileName = "dotnet",
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false
        };
        startInfo.ArgumentList.Add("run");
        startInfo.ArgumentList.Add("--project");
        startInfo.ArgumentList.Add(projectPath);
        startInfo.ArgumentList.Add("--");
        startInfo.ArgumentList.Add("--config");
        startInfo.ArgumentList.Add(E2eConfiguration.WriteArguments(configDir, name, arguments));

        var process = Process.Start(startInfo)
                      ?? throw new InvalidOperationException($"Failed to start {name}.");
        _ = CopyToFileAsync(process.StandardOutput, Path.Combine(logDir, $"{name}.stdout.log"));
        _ = CopyToFileAsync(process.StandardError, Path.Combine(logDir, $"{name}.stderr.log"));
        var dynamicProcess = new DynamicProcess(process, httpUrl, channelEndpoint);
        _processes.Add(dynamicProcess);
        return dynamicProcess;
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

    private static string PickEndpoint()
    {
        return $"tcp://127.0.0.1:{PickPort()}";
    }

    private static string PickHttpUrl()
    {
        return $"http://127.0.0.1:{PickPort()}";
    }

    private static int PickPort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
}

internal sealed record DynamicProvider(DynamicProcess Process, string HttpUrl, string ChannelEndpoint);

internal sealed record DynamicConsumer(DynamicProcess Process, string HttpUrl);

internal sealed class DynamicProcess(Process process, string httpUrl, string? channelEndpoint)
{
    private bool _disposed;

    public string HttpUrl { get; } = httpUrl;

    public string? ChannelEndpoint { get; } = channelEndpoint;

    public string RequireChannelEndpoint()
    {
        return ChannelEndpoint ??
               throw new InvalidOperationException("This process does not expose a channel endpoint.");
    }

    public async Task WaitReadyAsync()
    {
        using var client = ZLinkHttpClient.Create(HttpUrl).Build();
        for (var i = 0; i < 120; i++)
        {
            if (process.HasExited)
                throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");

            try
            {
                await client.Get("/health").Async<string>();
                return;
            }
            catch
            {
            }

            await Task.Delay(250);
        }

        throw new TimeoutException($"Process did not become ready: {HttpUrl}.");
    }

    public async Task StopAsync()
    {
        if (_disposed) return;

        _disposed = true;
        if (!process.HasExited)
            try
            {
                using var client = ZLinkHttpClient.Create(HttpUrl)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Build();
                await client.Post("/shutdown").AsyncRaw();
            }
            catch
            {
                if (!process.HasExited) process.Kill(true);
            }

        try
        {
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5));
        }
        catch (TimeoutException)
        {
            if (!process.HasExited) process.Kill(true);
            await process.WaitForExitAsync();
        }

        process.Dispose();
    }
}
