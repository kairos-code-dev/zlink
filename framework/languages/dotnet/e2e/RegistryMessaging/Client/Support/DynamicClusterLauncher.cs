using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Zlink.HttpClient;

namespace RegistryMessaging.Client.Support;

internal sealed class DynamicClusterLauncher(string providerProject, string logDir) : IAsyncDisposable
{
    private readonly List<DynamicProcess> _processes = [];

    public string RegistryRouterEndpoint { get; private set; } = "";

    public async ValueTask DisposeAsync()
    {
        for (var i = _processes.Count - 1; i >= 0; i--) await _processes[i].StopAsync();

        _processes.Clear();
    }

    public static async Task<DynamicClusterLauncher> StartAsync(ClientOptions options, string scenarioName)
    {
        var launcher = new DynamicClusterLauncher(options.ProviderProject, options.LogDir);
        try
        {
            launcher.RegistryRouterEndpoint = PickEndpoint();
            var registryHttp = PickHttpUrl();
            var registry = launcher.StartServer(
                $"{scenarioName}-registry",
                options.RegistryProject,
                [
                    "--rid", $"{scenarioName}-registry",
                    "--http-url", registryHttp,
                    "--registry-pub-endpoint", PickEndpoint(),
                    "--registry-router-endpoint", launcher.RegistryRouterEndpoint,
                    "--log-dir", options.LogDir
                ],
                registryHttp,
                null);
            await registry.WaitReadyAsync();
            return launcher;
        }
        catch
        {
            await launcher.DisposeAsync();
            throw;
        }
    }

    public async Task<DynamicProvider> StartProviderAsync(string name, string rid, int weight = 100)
    {
        var httpUrl = PickHttpUrl();
        var channelEndpoint = PickEndpoint();
        var process = StartServer(
            name,
            providerProject,
            [
                "--rid", rid,
                "--http-url", httpUrl,
                "--registry-router-endpoint", RegistryRouterEndpoint,
                "--channel-endpoint", channelEndpoint,
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
        foreach (var argument in arguments) startInfo.ArgumentList.Add(argument);

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
        using var client = new HttpClient();
        for (var i = 0; i < 120; i++)
        {
            if (process.HasExited)
                throw new InvalidOperationException($"Process exited before readiness: {process.ExitCode}.");

            try
            {
                using var response = await client.GetAsync($"{HttpUrl}/health");
                if (response.IsSuccessStatusCode) return;
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
                await client.Post("/shutdown").SubmitRawAsync();
            }
            catch
            {
                if (!process.HasExited) process.Kill(true);
            }

        await process.WaitForExitAsync();
        process.Dispose();
    }
}