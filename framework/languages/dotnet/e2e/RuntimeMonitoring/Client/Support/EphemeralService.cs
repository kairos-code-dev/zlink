using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using Zlink.Framework.E2E.Configuration;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Support;

internal sealed class EphemeralService : IAsyncDisposable
{
    private readonly Process _process;

    private EphemeralService(Process process, string url, string channelEndpoint)
    {
        _process = process;
        Url = url;
        ChannelEndpoint = channelEndpoint;
    }

    public string Url { get; }
    public string ChannelEndpoint { get; }

    public static async Task<EphemeralService> StartAsync(ClientOptions options, string rid)
    {
        var url = $"http://127.0.0.1:{ReservePort()}";
        var channelEndpoint = $"tcp://127.0.0.1:{ReservePort()}";
        var start = new ProcessStartInfo("dotnet") { UseShellExecute = false };
        start.ArgumentList.Add("run");
        start.ArgumentList.Add("--no-build");
        start.ArgumentList.Add("--project");
        start.ArgumentList.Add(options.ServiceProject);
        start.ArgumentList.Add("--");
        start.ArgumentList.Add("--config");
        var arguments = new List<string>
        {
            "--role", "service",
            "--rid", rid,
            "--http-url", url,
            "--redis-endpoint", options.RedisEndpoint,
            "--redis-key-prefix", options.RedisKeyPrefix,
            "--channel-endpoint", channelEndpoint,
            "--evidence-file", Path.Combine(options.LogDir, $"{rid}.evidence.log"),
            "--log-dir", options.LogDir
        };
        arguments.AddRange([
            "--spot-router-endpoint", $"tcp://127.0.0.1:{ReservePort()}",
            "--spot-pub-endpoint", $"tcp://127.0.0.1:{ReservePort()}"
        ]);
        start.ArgumentList.Add(E2eConfiguration.WriteArguments(options.ConfigDir, rid, arguments));
        var process = Process.Start(start)
                      ?? throw new InvalidOperationException($"Failed to start {rid}.");
        var service = new EphemeralService(process, url, channelEndpoint);
        await service.WaitForHealthAsync();
        return service;
    }

    public async ValueTask DisposeAsync()
    {
        if (!_process.HasExited)
        {
            try
            {
                using var http = ZLinkHttpClient.Create(Url).Timeout(TimeSpan.FromSeconds(3)).Build();
                await http.Post("/shutdown").AsyncRaw();
                await _process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
            }
            catch (Exception exception) when (
                exception is ZLinkFrameworkException or HttpRequestException or TaskCanceledException or TimeoutException)
            {
                if (!_process.HasExited) _process.Kill(true);
                await _process.WaitForExitAsync();
            }
        }
        _process.Dispose();
    }

    private async Task WaitForHealthAsync()
    {
        using var http = ZLinkHttpClient.Create(Url).Timeout(TimeSpan.FromMilliseconds(500)).Build();
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(3);
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                if ((await http.Get("/health").AsyncRaw()).Status == 200) return;
            }
            catch (Exception exception) when (
                exception is ZLinkFrameworkException or HttpRequestException or TaskCanceledException)
            {
            }
            await Task.Delay(50);
        }
        throw new TimeoutException($"{Url} did not become ready.");
    }

    private static int ReservePort()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        return ((IPEndPoint)listener.LocalEndpoint).Port;
    }
}
