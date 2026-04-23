using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Tests.Common;

namespace Zlink.Framework.MultiProcessTests;

[CollectionDefinition(nameof(MultiProcessTestsCollection), DisableParallelization = true)]
public sealed class MultiProcessTestsCollection
{
}

[Collection(nameof(MultiProcessTestsCollection))]
public sealed class TopologyMultiProcessTests
{
    [Fact]
    public async Task RemoteRegistryQueryClient_Reads_FrameworkTopology_From_TestHostProcesses()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var channelEndpoint = GetFreeTcpEndpoint();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);
        await using var frameworkHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-server",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--server-endpoint", channelEndpoint);

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddZLinkRegistryQueryClient(options =>
        {
            options.Endpoint = registryRouterEndpoint;
        });

        using var host = builder.Build();
        await host.StartAsync(timeout.Token);

        var client = host.Services.GetRequiredService<IZLinkRegistryQueryClient>();
        var snapshot = await RetryAsync(
            () => Task.FromResult(client.Snapshot()),
            entries => entries.Any(entry =>
                entry.ServiceName == "profile"
                && entry.Endpoint == channelEndpoint
                && entry.ServiceRole == ZLinkServiceRole.Router),
            TimeSpan.FromSeconds(15));

        Assert.Contains(snapshot, entry => entry.ServiceName == "profile");

        await host.StopAsync(timeout.Token);
    }

    [Fact]
    public async Task RegistryMonitoring_Emits_Topology_And_Summary_For_Remote_Framework_Process()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var channelEndpoint = GetFreeTcpEndpoint();

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<RegistryChangeProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkRegistryEvent>>(static provider =>
            provider.GetRequiredService<RegistryChangeProbe>());
        builder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var frameworkHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "channel-server",
            "--discovery-endpoint", registryRouterEndpoint,
            "--channel-name", "profile",
            "--server-endpoint", channelEndpoint);

        var probe = host.Services.GetRequiredService<RegistryChangeProbe>();
        await probe.WaitForTopologyAsync(TimeSpan.FromSeconds(15));
        await probe.WaitForSummaryAsync(TimeSpan.FromSeconds(15));

        await host.StopAsync();
    }

    [Fact]
    public async Task SpotMonitoring_Emits_Peers_And_Subjects_For_Remote_Process_Node()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var localNodeEndpoint = GetFreeTcpEndpoint();
        var remoteNodeEndpoint = GetFreeTcpEndpoint();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var registryHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "registry",
            "--registry-pub-endpoint", registryPubEndpoint,
            "--registry-router-endpoint", registryRouterEndpoint);

        var builder = Microsoft.Extensions.Hosting.Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotChangeProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkSpotEvent>>(static provider =>
            provider.GetRequiredService<SpotChangeProbe>());
        builder.Services.AddScoped<LocalMonitoringStageSubscriptionHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("local-stage-node", spot =>
            {
                spot.Bind(localNodeEndpoint);
                spot.EnablePubSub();
                spot.AddSpotFactory<LocalMonitoringStageSpot>("stage");
            });
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSpotEvents("local-stage-node", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync(timeout.Token);

        await using var remoteHost = await TestHostProcess.StartAsync(
            timeout.Token,
            "spot-node",
            "--discovery-channel", "game.stage",
            "--discovery-endpoint", registryRouterEndpoint,
            "--spot-node-name", "remote-stage-node",
            "--spot-bind-endpoint", remoteNodeEndpoint,
            "--enable-pubsub",
            "--spot-factory", "stage",
            "--create-spot", "stage");

        var probe = host.Services.GetRequiredService<SpotChangeProbe>();
        await probe.WaitForPeerAsync(TimeSpan.FromSeconds(15));
        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync("stage", timeout.Token);
        await probe.WaitForSubjectAsync(TimeSpan.FromSeconds(15));

        await host.StopAsync(timeout.Token);
    }

    private static string GetFreeTcpEndpoint()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }

    private static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("Multi-process retry timed out.");
    }

    public sealed class RegistryChangeProbe : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
    {
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _topology =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _summary =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkRegistryEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (@event.Event == ZLinkRegistryEventKind.TopologyChanged
                && @event.Topology is { Count: > 0 })
            {
                _topology.TrySetResult(@event);
            }

            if (@event.Event == ZLinkRegistryEventKind.ServiceSummaryChanged
                && @event.ServiceSummary is { Count: > 0 })
            {
                _summary.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkRegistryEvent> WaitForTopologyAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _topology.Task.WaitAsync(timeoutSource.Token);
        }

        public async Task<ZLinkRegistryEvent> WaitForSummaryAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _summary.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class SpotChangeProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _peer =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly TaskCompletionSource<ZLinkSpotEvent> _subject =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;

            if (@event.Event == ZLinkSpotEventKind.PeersChanged
                && @event.Peers is { Count: > 0 })
            {
                _peer.TrySetResult(@event);
            }

            if (@event.Event == ZLinkSpotEventKind.SubjectsChanged
                && @event.Subjects is { Count: > 0 })
            {
                _subject.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitForPeerAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _peer.Task.WaitAsync(timeoutSource.Token);
        }

        public async Task<ZLinkSpotEvent> WaitForSubjectAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _subject.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class LocalMonitoringStageSpot : ZLinkSpot
    {
        public LocalMonitoringStageSpot(
            global::Zlink.RoutingId spotRid,
            global::Zlink.RoutingId nodeRid)
            : base(spotRid, nodeRid)
        {
            AddSubscribe<LocalMonitoringStageSubscriptionHandler>("stage.monitor");
        }
    }

    public sealed record LocalMonitoringStageEvent(string Value);

    public sealed class LocalMonitoringStageSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<LocalMonitoringStageSpot, LocalMonitoringStageEvent>
    {
        public ValueTask HandleAsync(
            LocalMonitoringStageSpot spot,
            LocalMonitoringStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = message;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }
}
