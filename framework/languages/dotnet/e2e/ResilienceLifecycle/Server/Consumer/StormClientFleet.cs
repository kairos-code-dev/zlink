using Microsoft.Extensions.Configuration;
using ResilienceLifecycle.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;

namespace ResilienceLifecycle.Server.Consumer;

internal sealed class StormClientFleet(ConsumerOptions options) : IAsyncDisposable
{
    private const int ClientCount = 100;
    private readonly ConnectionEvidence _connections = new();
    private readonly List<IHost> _hosts = [];
    private IReadOnlyList<IZLinkChannelClient> _clients = [];

    public int ConnectionCount => _connections.Snapshot().Length;

    public async Task<ProfileRes[]> StartAndRequestAsync(string markerPrefix, CancellationToken cancellationToken)
    {
        if (_hosts.Count == 0)
        {
            for (var index = 0; index < ClientCount; index++) _hosts.Add(CreateHost(index));
            await Task.WhenAll(_hosts.Select(host => host.StartAsync(cancellationToken)));
            _clients = _hosts.Select(host => host.Services.GetRequiredService<IZLinkChannelClient>()).ToArray();
        }

        return await RequestAllAsync(markerPrefix, cancellationToken);
    }

    public async Task<ProfileRes[]> RequestAllAsync(string markerPrefix, CancellationToken cancellationToken)
    {
        if (_clients.Count != ClientCount) throw new InvalidOperationException("Storm fleet is not started.");
        return await Task.WhenAll(_clients.Select((client, index) => client
            .RequestToChannel(ResilienceLifecycleNames.Channel, new ProfileReq("fast", $"{markerPrefix}-{index}"))
            .Timeout(TimeSpan.FromSeconds(10))
            .Async<ProfileRes>(cancellationToken)
            .AsTask()));
    }

    public async Task<string[]> WaitReadyAsync(int afterCount, CancellationToken cancellationToken)
    {
        var expected = Enumerable.Range(0, ClientCount)
            .Select(index => $"client=storm-{index}|kind=ConnectionReady")
            .ToArray();
        return await _connections.WaitAllAsync(expected, afterCount, TimeSpan.FromSeconds(30), cancellationToken);
    }

    public ValueTask DisposeAsync()
    {
        var hosts = _hosts.ToArray();
        _hosts.Clear();
        _clients = [];
        return new ValueTask(StopAllAsync(hosts));
    }

    private IHost CreateHost(int index)
    {
        return Host.CreateDefaultBuilder()
            .ConfigureAppConfiguration((_, configuration) => configuration.Sources.Clear())
            .ConfigureServices(services =>
            {
                services.AddSingleton(_connections);
                services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>>(
                    _ => new StormConnectionEventObserver(index, _connections));
                services.AddZLinkFramework(framework =>
                {
                    framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                        .SetConnectionString(options.RedisEndpoint)
                        .SetKeyPrefix(options.RedisKeyPrefix)));
                    framework.ConfigureDispatch()
                        .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                        .TraceLogFile(Path.Combine(options.LogDir, $"storm-{index}-flow.log"))
                        .TraceLabel($"storm-{index}");
                    framework.AddClientServerChannel(ResilienceLifecycleNames.Channel).EnableClient();
                });
                services.AddZLinkMonitoring(monitor => monitor.AddSocketEvents(
                    "resilience.profile.client",
                    ZLinkSocketEventKind.ConnectionReady,
                    ZLinkSocketEventKind.Disconnected));
            })
            .Build();
    }

    private static async Task StopAllAsync(IEnumerable<IHost> hosts)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(15));
        await Task.WhenAll(hosts.Select(async host =>
        {
            try
            {
                await host.StopAsync(timeout.Token);
            }
            catch (OperationCanceledException) when (timeout.IsCancellationRequested)
            {
            }
            finally
            {
                host.Dispose();
            }
        }));
    }
}

internal sealed class StormConnectionEventObserver(int index, ConnectionEvidence evidence)
    : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"client=storm-{index}|kind={@event.Event}"
            + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}");
        return ValueTask.CompletedTask;
    }
}
