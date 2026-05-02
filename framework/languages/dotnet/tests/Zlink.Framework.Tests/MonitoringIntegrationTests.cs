using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Net;
using System.Net.Sockets;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

[Collection(nameof(FrameworkRuntimeIntegrationTestsCollection))]
public sealed class MonitoringIntegrationTests
{
    [Fact]
    public async Task RegistryMonitoring_Emits_StatusChanged_For_EmbeddedRegistry()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddSingleton<RegistryMonitorProbe>();
        registryBuilder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkRegistryEvent>>(static provider =>
            provider.GetRequiredService<RegistryMonitorProbe>());
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        registryBuilder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(100));
        });

        using var registryHost = registryBuilder.Build();
        await registryHost.StartAsync();

        var probe = registryHost.Services.GetRequiredService<RegistryMonitorProbe>();
        var @event = await probe.WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(ZLinkRegistryEventKind.StatusChanged, @event.Event);
        Assert.Equal("registry", @event.SourceName);
        Assert.NotNull(@event.Status);
        Assert.True(@event.Status?.State is ZLinkRegistryState.Idle or ZLinkRegistryState.Active);

        await StopHostsAsync(registryHost);
    }

    [Fact]
    public async Task SpotMonitoring_Emits_SubjectsChanged_When_SpotIsCreated()
    {
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var builder = Host.CreateApplicationBuilder();

        builder.Services.AddSingleton<SpotMonitorProbe>();
        builder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkSpotEvent>>(static provider =>
            provider.GetRequiredService<SpotMonitorProbe>());
        builder.Services.AddScoped<StageSubscriptionHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.AddSpotFactory<MonitoringStageSpot>("stage");
            });
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSpotEvents("stage-node", TimeSpan.FromMilliseconds(100));
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync("stage");

        var @event = await host.Services.GetRequiredService<SpotMonitorProbe>()
            .WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Equal(ZLinkSpotEventKind.SubjectsChanged, @event.Event);
        Assert.NotNull(@event.Subjects);
        Assert.Contains(@event.Subjects!, subject => subject.Subject == "stage.monitor");

        await StopHostsAsync(host);
    }

    [Fact]
    public async Task RegistryMonitoring_Emits_Topology_And_ServiceSummary_When_FrameworkHostRegisters()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var apiEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddSingleton<RegistryChangeProbe>();
        registryBuilder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkRegistryEvent>>(static provider =>
            provider.GetRequiredService<RegistryChangeProbe>());
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        registryBuilder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(100));
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddClientServerChannel("profile", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var probe = registryHost.Services.GetRequiredService<RegistryChangeProbe>();
        await probe.WaitForTopologyAsync(TimeSpan.FromSeconds(10));
        await probe.WaitForSummaryAsync(TimeSpan.FromSeconds(10));

        await StopHostsAsync(frameworkHost, registryHost);
    }

    [Fact]
    public async Task SpotMonitoring_Emits_PeersChanged_When_RemoteNodeAppears()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var firstNodeEndpoint = GetFreeTcpEndpoint();
        var secondNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var firstBuilder = Host.CreateApplicationBuilder();
        firstBuilder.Services.AddSingleton<SpotPeerProbe>();
        firstBuilder.Services.AddSingleton<IZLinkRuntimeEventHandler<ZLinkSpotEvent>>(static provider =>
            provider.GetRequiredService<SpotPeerProbe>());
        firstBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind(firstNodeEndpoint);
            });
        });
        firstBuilder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSpotEvents("stage-node", TimeSpan.FromMilliseconds(100));
        });

        var secondBuilder = Host.CreateApplicationBuilder();
        secondBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("remote-stage-node", spot =>
            {
                spot.Bind(secondNodeEndpoint);
            });
        });

        using var registryHost = registryBuilder.Build();
        using var firstHost = firstBuilder.Build();
        using var secondHost = secondBuilder.Build();

        await registryHost.StartAsync();
        await firstHost.StartAsync();
        await secondHost.StartAsync();

        var peerEvent = await firstHost.Services.GetRequiredService<SpotPeerProbe>()
            .WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(ZLinkSpotEventKind.PeersChanged, peerEvent.Event);
        Assert.NotNull(peerEvent.Peers);
        Assert.NotEmpty(peerEvent.Peers!);

        await StopHostsAsync(secondHost, firstHost, registryHost);
    }

    private static string GetFreeTcpEndpoint()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        var endpoint = (IPEndPoint)listener.LocalEndpoint;
        return $"tcp://127.0.0.1:{endpoint.Port}";
    }

    public sealed class RegistryMonitorProbe : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
    {
        private readonly TaskCompletionSource<ZLinkRegistryEvent> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkRegistryEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (@event.Event == ZLinkRegistryEventKind.StatusChanged
                && @event.Status is not null)
            {
                _completion.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkRegistryEvent> WaitAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _completion.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class SpotMonitorProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (@event.Event == ZLinkSpotEventKind.SubjectsChanged
                && @event.Subjects is { Count: > 0 })
            {
                _completion.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _completion.Task.WaitAsync(timeoutSource.Token);
        }
    }

    private static async Task StopHostsAsync(params IHost[] hosts)
    {
        foreach (var host in hosts)
        {
            await host.StopAsync();
        }

        await Task.Delay(500);
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

    public sealed class SpotPeerProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (@event.Event == ZLinkSpotEventKind.PeersChanged
                && @event.Peers is { Count: > 0 })
            {
                _completion.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _completion.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class MonitoringStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<StageSubscriptionHandler>("stage.monitor");
        }
    }

    public sealed record StageSubscriptionMessage(string Value);

    public sealed class StageSubscriptionHandler
        : IZLinkSpotSubscriptionHandler<MonitoringStageSpot, StageSubscriptionMessage>
    {
        public ValueTask HandleAsync(
            MonitoringStageSpot spot,
            StageSubscriptionMessage @event,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = @event;
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }
}
