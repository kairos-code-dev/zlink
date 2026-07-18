using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.UnitTests;

public sealed class MonitoringTests : RegistrationValidationSupport
{
    [Fact]
    public void RemovedSpotEgressClient_DI_IsNotExposed_AsPublicCapability()
    {
        var withoutEgress = new ServiceCollection();
        withoutEgress.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("gateway.client");
                channel.EnableClient("tcp://127.0.0.1:7101");
            }
        });

        using (var provider = withoutEgress.BuildServiceProvider())
        {
            _ = provider;
            Assert.DoesNotContain(typeof(IZLinkSpotOutbound).Assembly.GetTypes(), IsRemovedSpotEgressClient);
        }

        var routeMeshEgress = new ServiceCollection();
        routeMeshEgress.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddRouteMeshChannel("gateway.route");
                channel.EnableServer("tcp://127.0.0.1:7301");
                channel.EnableClient("tcp://127.0.0.1:7201");
            }
        });

        using (var provider = routeMeshEgress.BuildServiceProvider())
        {
            _ = provider;
            Assert.DoesNotContain(typeof(IZLinkSpotOutbound).Assembly.GetTypes(), IsRemovedSpotEgressClient);
        }
    }

    private static bool IsRemovedSpotEgressClient(Type type)
    {
        return type.Namespace == "Zlink.Framework.Contracts.Spots"
               && type.Name.Contains("Routed", StringComparison.Ordinal)
               && type.Name.Contains("Spot", StringComparison.Ordinal)
               && type.Name.Contains("Client", StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenPublisherHasNoBindEndpoint()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options => { options.AddFanoutChannel("profile").EnablePublisher(""); }));

        Assert.Contains("Channel publisher bind endpoint must not be empty", exception.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_RegistersValidatedConfigurationAndFilterTypes()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.DefaultRequestTimeout = TimeSpan.FromSeconds(5);
            options.Codecs.Use(ZLinkProtobufCodec.Default);
            options.UseFilter<TestFilter>();
            options.UseTestLocationStore();

            {
                var channel = options.AddClientServerChannel("profile");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.EnableClient();
                channel.AddRequestHandler<TestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
            }

            {
                var events = options.AddFanoutChannel("profile.events");
                events.EnableSubscriber();
                events.AddPublishHandler<TestPublishHandler, TestPublishedEvent>();
            }

            {
                var stream = options.AddStreamNode("stream.node");
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestHeaderSession>();
            }

            {
                var routed = options.AddRouteMeshChannel("gateway");
                routed.EnableServer("tcp://127.0.0.1:7301");
            }

            {
                var mesh = options.AddRouteMesh("game.stage");
                mesh.ChannelName("game.stage");
                {
                    var spot = mesh;
                    {
                        var router = spot.Listen("tcp://127.0.0.1:9000");
                    }
                    spot.AddSpotFactory<TestSpot>();
                }
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var filter = provider.GetRequiredService<TestFilter>();

        Assert.NotNull(filter);
        Assert.Equal(TimeSpan.FromSeconds(5), registration.DefaultRequestTimeout);
        Assert.Equal(TimeSpan.FromMilliseconds(1000), registration.DefaultSocketSendTimeout);
        Assert.Contains("application/x-protobuf", registration.Codecs.Serializers.Keys);
        Assert.True(registration.Locations.Enabled);
        Assert.NotNull(registration.SpotDiscovery);
        Assert.Contains("profile", registration.Channels.Keys);
        Assert.Contains("stream.node", registration.StreamNodes.Keys);
        Assert.Contains("game.stage", registration.SpotNodes.Keys);
    }

    [Fact]
    public void ChannelDefaultRequestTimeoutOverridesGlobalDefault()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.DefaultRequestTimeout = TimeSpan.FromSeconds(30);
            options.AddClientServerChannel("api")
                .EnableClient("tcp://127.0.0.1:7101")
                .SetDefaultRequestTimeout(TimeSpan.FromSeconds(2));
            options.AddRouteMeshChannel("route")
                .EnableServer("tcp://127.0.0.1:7201")
                .EnableClient("tcp://127.0.0.1:7202")
                .SetDefaultRequestTimeout(TimeSpan.FromSeconds(3));
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();

        Assert.Equal(TimeSpan.FromSeconds(2), registration.ResolveChannelRequestTimeout("api"));
        Assert.Equal(TimeSpan.FromSeconds(3), registration.ResolveRouteRequestTimeout("route"));
        Assert.Equal(TimeSpan.FromSeconds(30), registration.ResolveChannelRequestTimeout("missing"));
        Assert.Equal(TimeSpan.FromSeconds(30), registration.ResolveRouteRequestTimeout("missing"));
    }

    [Fact]
    public async Task AddZLinkMonitoring_Throws_WhenSocketSourceIsUnknownOnStartup()
    {
        var builder = Host.CreateApplicationBuilder();

        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("profile.server", ZLinkSocketEventKind.ConnectionReady);
        });

        using var host = builder.Build();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("AddZLinkFramework", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task AddZLinkMonitoring_Throws_WhenSocketSourceDoesNotMatchRegisteredCapability()
    {
        var endpoint = "tcp://127.0.0.1:7101";
        var builder = Host.CreateApplicationBuilder();

        builder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("profile");
                channel.EnableServer(endpoint);
                channel.AddRequestHandler<TestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
            }
        });

        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("orders.server", ZLinkSocketEventKind.ConnectionReady);
        });

        using var host = builder.Build();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("not registered", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task AddZLinkMonitoring_Throws_WhenSpotSourceDoesNotMatchRegisteredSpotNode()
    {
        var builder = Host.CreateApplicationBuilder();
        var endpoint = $"inproc://monitoring-{Guid.NewGuid():N}";

        builder.Services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("game.stage").Listen(endpoint).ChannelName("game.stage");
        });
        builder.Services.AddZLinkMonitoring(options =>
        {
            options.AddSpotEvents("stage-node", TimeSpan.FromSeconds(1));
        });
        builder.Services.AddSingleton<IZLinkBackendAdapterFactory, FailIfRuntimeStartsBackendAdapterFactory>();

        using var host = builder.Build();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("SPOT node 'stage-node' is not registered", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkMonitoring_RequiresPositivePollingIntervals()
    {
        var registration = new ZLinkMonitoringRegistration();
        var options = new ZLinkMonitoringOptionsModel(registration);

        var spot = Assert.Throws<ZLinkConfigurationException>(() =>
            options.AddSpotEvents("game.stage", TimeSpan.Zero));
        var location = Assert.Throws<ZLinkConfigurationException>(() =>
            options.AddLocationRuntimeEvents("locations", TimeSpan.FromMilliseconds(-1)));

        Assert.Contains("greater than zero", spot.Message, StringComparison.Ordinal);
        Assert.Contains("greater than zero", location.Message, StringComparison.Ordinal);
        Assert.Empty(registration.SpotSources);
        Assert.Empty(registration.LocationRuntimeSources);
    }

    [Fact]
    public void MonitoringSourceValidator_RequiresLocationRuntimeForLocationSources()
    {
        var registration = new ZLinkMonitoringRegistration();
        new ZLinkMonitoringOptionsModel(registration)
            .AddLocationRuntimeEvents("locations", TimeSpan.FromSeconds(1));
        var validator = new ZLinkMonitoringSourceValidator(registration);

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            validator.ValidateRequiredRuntimes(frameworkRuntime: null, locationQuery: null));

        Assert.Contains("location stores", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkMonitoring_UsesExplicitSpotSourceWithoutAutoDiscovery()
    {
        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            options.AddRouteMesh("game.stage").Listen("tcp://127.0.0.1:9000").ChannelName("game.stage");
        });
        services.AddZLinkMonitoring(options =>
        {
            options.AddSpotEvents("game.stage", TimeSpan.FromSeconds(1));
        });

        using var provider = services.BuildServiceProvider();
        var framework = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var monitoring = provider.GetRequiredService<ZLinkMonitoringRegistration>();

        Assert.Equal("game.stage", Assert.Single(framework.SpotNodes.Keys));
        Assert.Equal("game.stage", Assert.Single(monitoring.SpotSources.Keys));
    }

    [Fact]
    public void SpotPollingEventDiff_EmitsSealedVariantsOnlyForChangedSnapshotParts()
    {
        var timestamp = DateTimeOffset.Parse("2026-07-12T00:00:00Z");
        var diff = new ZLinkSpotPollingEventDiff("game.stage");
        var events = new List<ZLinkSpotEvent>();
        var initial = Snapshot();

        diff.DispatchChanges(initial, timestamp, events.Add);

        Assert.Collection(
            events,
            item => Assert.IsType<ZLinkSpotEvent.StatusChanged>(item),
            item => Assert.IsType<ZLinkSpotEvent.PeersChanged>(item),
            item => Assert.IsType<ZLinkSpotEvent.SubjectsChanged>(item));
        Assert.All(events, item => Assert.Equal("game.stage", item.SourceName));

        events.Clear();
        diff.DispatchChanges(initial, timestamp.AddSeconds(1), events.Add);
        Assert.Empty(events);

        var changed = initial with
        {
            Peers =
            [
                new ZLinkSpotNodePeerEntry(
                    "game.stage",
                    "tcp://127.0.0.1:9000",
                    "tcp://127.0.0.1:9001",
                    ZLinkSpotPeerSource.Manual,
                    ZLinkSpotPeerKind.SpotMesh,
                    ZLinkSpotPeerState.Connected,
                    100,
                    1,
                    2)
            ]
        };

        diff.DispatchChanges(changed, timestamp.AddSeconds(2), events.Add);

        var peersChanged = Assert.IsType<ZLinkSpotEvent.PeersChanged>(Assert.Single(events));
        Assert.Equal(changed.Peers, peersChanged.Peers);

        events.Clear();
        var statusAndSubjectsChanged = changed with
        {
            Status = changed.Status with { ActivePeerCount = 1, LastChangedMs = 3 },
            Subjects =
            [
                new ZLinkSpotNodeSubjectEntry(
                    ZLinkSpotRole.Sub,
                    "players.*",
                    ZLinkSubjectKind.Pattern,
                    1,
                    1,
                    3)
            ]
        };

        diff.DispatchChanges(statusAndSubjectsChanged, timestamp.AddSeconds(3), events.Add);

        Assert.Collection(
            events,
            item => Assert.IsType<ZLinkSpotEvent.StatusChanged>(item),
            item => Assert.IsType<ZLinkSpotEvent.SubjectsChanged>(item));
    }

    private static ZLinkSpotMonitoringSnapshot Snapshot()
    {
        return new ZLinkSpotMonitoringSnapshot(
            new ZLinkSpotNodeStatus(
                "game.stage",
                "tcp://127.0.0.1:9000",
                RoutingId.From("stage-node"),
                ZLinkSpotNodeState.Ready,
                0,
                0,
                0,
                0,
                0,
                0,
                1),
            [],
            []);
    }

    private sealed class FailIfRuntimeStartsBackendAdapterFactory : IZLinkBackendAdapterFactory
    {
        public IZLinkChannelBackendAdapter CreateChannelAdapter() =>
            throw new InvalidOperationException("Static monitoring validation must run before native startup.");

        public IZLinkSpotBackendAdapter CreateSpotAdapter() =>
            throw new InvalidOperationException("Static monitoring validation must run before native startup.");

        public IZLinkStreamBackendAdapter CreateStreamAdapter() =>
            throw new InvalidOperationException("Static monitoring validation must run before native startup.");

        public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => new UnusedMonitoringBackendAdapter();
    }

    private sealed class UnusedMonitoringBackendAdapter : IZLinkMonitoringBackendAdapter
    {
        public IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket) =>
            throw new InvalidOperationException("Static monitoring validation must not open a monitor.");
    }
}
