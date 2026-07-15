using System.Collections.Concurrent;
using System.Net.Sockets;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using RuntimeMonitoring.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;

namespace RuntimeMonitoring.Client.Support;

internal sealed class MonitoringChannelClient : IAsyncDisposable
{
    private readonly IHost _host;
    private readonly ClientEvidenceStore _evidence;

    private MonitoringChannelClient(IHost host, ClientEvidenceStore evidence)
    {
        _host = host;
        _evidence = evidence;
    }

    public static async Task<MonitoringChannelClient> StartAsync(
        ClientOptions options,
        string endpoint,
        string traceLabel,
        bool monitorSocketEvents = false)
    {
        var evidence = new ClientEvidenceStore();
        var host = Host.CreateDefaultBuilder()
            .ConfigureServices(services =>
            {
                services.AddSingleton(evidence);
                services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, ClientSocketEventRecorder>();
                services.AddZLinkFramework(framework =>
                {
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                        .TraceLabel(traceLabel);
                    framework.AddClientServerChannel(RuntimeMonitoringNames.Channel).EnableClient(endpoint);
                });
                if (monitorSocketEvents)
                    services.AddZLinkMonitoring(monitor =>
                        monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelClientSource));
            })
            .Build();
        await host.StartAsync();
        return new MonitoringChannelClient(host, evidence);
    }

    public static async Task<MonitoringChannelClient> StartAutoConnectedAsync(
        ClientOptions options,
        string traceLabel)
    {
        var evidence = new ClientEvidenceStore();
        var host = Host.CreateDefaultBuilder()
            .ConfigureServices(services =>
            {
                services.AddSingleton(evidence);
                services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, ClientSocketEventRecorder>();
                services.AddZLinkFramework(framework =>
                {
                    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                        .SetConnectionString(options.RedisEndpoint)
                        .SetKeyPrefix(options.RedisKeyPrefix)));
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"{traceLabel}-flow.log"))
                        .TraceLabel(traceLabel);
                    framework.AddClientServerChannel(RuntimeMonitoringNames.Channel).EnableClient();
                });
                services.AddZLinkMonitoring(monitor =>
                    monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelClientSource));
            })
            .Build();
        await host.StartAsync();
        return new MonitoringChannelClient(host, evidence);
    }

    public async Task<ProfileRes> RequestAsync(ProfileReq request)
    {
        var channel = _host.Services.GetRequiredService<IZLinkChannelClient>();
        return await channel.RequestToChannel(RuntimeMonitoringNames.Channel, request)
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<ProfileRes>();
    }

    public Task<string[]> WaitForEvidenceAsync(
        Func<string[], bool> predicate,
        CancellationToken cancellationToken = default) =>
        _evidence.WaitUntilAsync(predicate, TimeSpan.FromSeconds(30), cancellationToken);

    public string[] GetEvidence() => _evidence.Snapshot();

    public async ValueTask DisposeAsync()
    {
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        try
        {
            await _host.StopAsync(cts.Token);
        }
        catch (OperationCanceledException)
        {
        }
        _host.Dispose();
    }

    public static async Task SendInvalidHandshakeAsync(string endpoint)
    {
        var uri = new Uri(endpoint);
        using var client = new TcpClient();
        await client.ConnectAsync(uri.Host, uri.Port);
        var stream = client.GetStream();
        var invalidGreeting = new byte[]
        {
            0x00, 0x01, 0x02, 0x00,
            0x00, 0x00, 0x00, 0x03,
            0x01, 0x00, 0x00
        };
        await stream.WriteAsync(invalidGreeting);
        await stream.FlushAsync();
        client.Client.Shutdown(SocketShutdown.Send);
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
        var response = new byte[1];
        try
        {
            _ = await stream.ReadAsync(response, timeout.Token);
        }
        catch (IOException)
        {
            // A protocol rejection may reset the connection instead of returning EOF.
        }
    }
}

internal sealed class ClientSocketEventRecorder(ClientEvidenceStore evidence)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"monitor-socket|source={@event.SourceName}|kind={@event.Event}"
                     + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}"
                     + $"|native={@event.Diagnostic?.NativeEvent}|value={@event.Diagnostic?.NativeValue}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ClientEvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly SemaphoreSlim _changed = new(0);

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        _changed.Release();
    }

    public string[] Snapshot() => _entries.ToArray();

    public async Task<string[]> WaitUntilAsync(
        Func<string[], bool> predicate,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + timeout;
        while (true)
        {
            var snapshot = _entries.ToArray();
            if (predicate(snapshot)) return snapshot;
            var remaining = deadline - DateTimeOffset.UtcNow;
            if (remaining <= TimeSpan.Zero)
                throw new TimeoutException(
                    "Timed out waiting for monitoring client evidence: "
                    + string.Join(";", snapshot));
            await _changed.WaitAsync(remaining, cancellationToken);
        }
    }
}
