using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Protocol;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

public sealed class RegistrationValidationTests
{
    [Fact]
    public void AddZLinkFramework_Throws_WhenChannelNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("profile", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                });
                options.AddClientServerChannel("profile", channel =>
                {
                    channel.EnableClient();
                });
            }));

        Assert.Contains("Duplicate channel name", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenCompatibilityChannelMixesAutoConnectTypes()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddChannel("profile", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                    channel.EnablePublisher(publisher => publisher.Bind("tcp://127.0.0.1:7102"));
                });
            }));

        Assert.Contains("mixes client/server and fanout capabilities", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsChannelClientManualConnections_WhenDiscoveryIsConfigured()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("profile", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7101"));
                });
            });

            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
        });
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteChannelMixesDiscoveryAndManualConnections()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.AddRouteMeshChannel("backend", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:7201");
                    routed.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7202"));
                });
            }));

        Assert.Contains("cannot mix discovery and manual connections", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenClientHasNoPeerAcquisitionPath()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("profile", channel => channel.EnableClient());
            }));

        Assert.Contains("requires discovery or manual connections", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleHeaderSessions()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind("tcp://127.0.0.1:9100");
                    stream.AddHeaderSession<TestHeaderSession>();
                    stream.AddHeaderSession<TestHeaderSession>();
                });
            }));

        Assert.Contains("already has a stream session", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotNodeHasNoSpotDiscovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotNode("stage-node", spot =>
                {
                    spot.Bind("tcp://127.0.0.1:9000");
                    spot.AddSpotFactory<TestSpot>("stage");
                });
            }));

        Assert.Contains("requires AddSpotMesh", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotFactoryNameIsDuplicatedAcrossNodes()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseSpotDiscovery("game.stage", discovery => discovery.Add("tcp://127.0.0.1:5551"));

                options.AddSpotNode("stage-node-a", spot =>
                {
                    spot.Bind("tcp://127.0.0.1:6101");
                    spot.AddSpotFactory<TestSpot>("stage");
                });

                options.AddSpotNode("stage-node-b", spot =>
                {
                    spot.Bind("tcp://127.0.0.1:6102");
                    spot.AddSpotFactory<TestSpot>("stage");
                });
            }));

        Assert.Contains("Duplicate SPOT factory 'stage'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotNodeRegistersMultipleEntrySpots()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseSpotDiscovery("game.stage", _ => { });

                options.AddSpotNode("stage-node", spot =>
                {
                    spot.Bind("tcp://127.0.0.1:6101");
                    spot.AddEntrySpot<TestEntrySpot>();
                    spot.AddEntrySpot<TestEntrySpot>();
                });
            }));

        Assert.Contains("Duplicate Entry Spot registry", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenActorFactoryNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<TestActorFactory>("warrior");
                options.AddActorFactory<TestActorFactory>("warrior");
            }));

        Assert.Contains("Duplicate actor factory 'warrior'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenServerHasNoBindEndpoint()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("profile", channel => channel.EnableServer());
            }));

        Assert.Contains("server must define a bind endpoint", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenPublisherHasNoBindEndpoint()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddFanoutChannel("profile", channel => channel.EnablePublisher());
            }));

        Assert.Contains("publisher must define a bind endpoint", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_RegistersValidatedConfigurationAndFilterTypes()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.DefaultTimeout = TimeSpan.FromSeconds(5);
            options.Codecs.AddProtobuf();
            options.UseFilter<TestFilter>();
            options.ConfigureDispatch(dispatch =>
            {
                dispatch.SpotDispatchMode = ZLinkDispatchMode.Dynamic;
            });
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.UseSpotDiscovery("game.stage", discovery => discovery.Add("tcp://127.0.0.1:5551"));

            options.AddClientServerChannel("profile", channel =>
            {
                channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                channel.EnableClient();
            });

            options.AddFanoutChannel("profile.events", events =>
            {
                events.EnableSubscriber();
            });

            options.AddStreamNode("stream.node", stream =>
            {
                stream.Bind("tcp://127.0.0.1:9100");
                stream.AddHeaderSession<TestHeaderSession>();
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:9000");
                spot.AddSpotFactory<TestSpot>("stage");
                spot.AttachClientServerChannelClient("profile");
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var filter = provider.GetRequiredService<TestFilter>();

        Assert.NotNull(filter);
        Assert.Equal(TimeSpan.FromSeconds(5), registration.DefaultTimeout);
        Assert.Contains("protobuf", registration.Codecs.RegisteredCodecs);
        Assert.Equal(ZLinkDispatchMode.Dynamic, registration.DispatchOptions.SpotDispatchMode);
        Assert.NotNull(registration.Discovery);
        Assert.NotNull(registration.SpotDiscovery);
        Assert.Contains("profile", registration.Channels.Keys);
        Assert.Contains("stream.node", registration.StreamNodes.Keys);
        Assert.Contains("stage-node", registration.SpotNodes.Keys);
    }

    [Fact]
    public void AddZLinkRegistry_Throws_WhenPubEndpointIsMissing()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkRegistry(options =>
            {
                options.RouterEndpoint = "tcp://127.0.0.1:5551";
            }));

        Assert.Contains("pub endpoint", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void AddZLinkRegistry_Throws_WhenRouterEndpointIsMissing()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkRegistry(options =>
            {
                options.PubEndpoint = "tcp://127.0.0.1:5550";
            }));

        Assert.Contains("router endpoint", exception.Message, StringComparison.OrdinalIgnoreCase);
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
            options.AddClientServerChannel("profile", channel =>
            {
                channel.EnableServer(server => server.Bind(endpoint));
            });
        });

        builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents("orders.server", ZLinkSocketEventKind.ConnectionReady);
        });

        using var host = builder.Build();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() => host.StartAsync());

        Assert.Contains("not registered", exception.Message, StringComparison.Ordinal);
    }

    private sealed class TestFilter : IZLinkHandlerFilter
    {
        public ValueTask<object?> InvokeAsync(
            ZLinkHandlerInvocation invocation,
            ZLinkHandlerDelegate next,
            CancellationToken cancellationToken)
        {
            return next(cancellationToken);
        }
    }

    private sealed class TestHeaderSession : IZLinkSession
    {
        public IZLinkSessionContext Context { get; set; } = default!;

        public ValueTask OnConnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken) => ValueTask.CompletedTask;

        public ValueTask OnDispatchAsync(
            ZlinkStreamHeader header,
            global::Systems.Zlink.Message body,
            CancellationToken cancellationToken) => ValueTask.CompletedTask;
    }

    private sealed class TestSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;
    }

    private sealed class TestEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;
    }

    private sealed class TestActorFactory : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId));
        }
    }

    private sealed class TestActor(string actorId) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; set; } = default!;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }
}
