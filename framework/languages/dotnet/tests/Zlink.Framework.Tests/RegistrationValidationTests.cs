using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
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
    public void AddChannel_And_AddRouteChannel_Are_Removed_From_Public_Surface()
    {
        Assert.Null(typeof(IZLinkFrameworkOptions).GetMethod("AddChannel"));
        Assert.Null(typeof(IZLinkFrameworkOptions).GetMethod("AddRouteChannel"));
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsFanoutChannel()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.AddFanoutChannel("events", channel =>
                {
                    channel.EnablePublisher(publisher => publisher.Bind("tcp://127.0.0.1:7102"));
                });
                options.AddSpotMesh("spot.mesh", mesh =>
                {
                    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                    mesh.AddNode("stage-node", node =>
                    {
                        node.Bind("tcp://127.0.0.1:9000");
                        node.EnableRouter();
                        node.AcceptSpotRoutesFromChannel(
                            "events",
                            routes => routes.UseManualConnections(
                                peers => peers.Connect("tcp://127.0.0.1:7102")));
                    });
                });
            }));

        Assert.Contains("must be a client/server channel or a route mesh channel", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsDealerMeshChannel()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.AddDealerMeshChannel("mesh", channel => channel.EnableClient());
                options.AddSpotMesh("spot.mesh", mesh =>
                {
                    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                    mesh.AddNode("stage-node", node =>
                    {
                        node.Bind("tcp://127.0.0.1:9000");
                        node.EnableRouter();
                        node.AcceptSpotRoutesFromChannel(
                            "mesh",
                            routes => routes.UseManualConnections(
                                peers => peers.Connect("tcp://127.0.0.1:7103")));
                    });
                });
            }));

        Assert.Contains("must be a client/server channel or a route mesh channel", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsAmbiguousChannelName()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.AddClientServerChannel("route", channel => channel.EnableClient());
                options.AddRouteMeshChannel("route", routed => routed.Bind("tcp://127.0.0.1:7104"));
                options.AddSpotMesh("spot.mesh", mesh =>
                {
                    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                    mesh.AddNode("stage-node", node =>
                    {
                        node.Bind("tcp://127.0.0.1:9000");
                        node.EnableRouter();
                        node.AcceptSpotRoutesFromChannel(
                            "route",
                            routes => routes.UseManualConnections(
                                peers => peers.Connect("tcp://127.0.0.1:7104")));
                    });
                });
            }));

        Assert.Contains("ambiguous", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RejectsUnknownChannel()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseSpotDiscovery("spot.mesh", _ => { });
                options.AddSpotNode("stage-node", node =>
                {
                    node.Bind("tcp://127.0.0.1:9000");
                    node.EnableRouter();
                    node.AcceptSpotRoutesFromChannel(
                        "missing",
                        routes => routes.UseManualConnections(
                            peers => peers.Connect("tcp://127.0.0.1:7104")));
                });
            }));

        Assert.Contains("is not registered", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RequiresEnableRouter()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel(
                    "api",
                    channel => channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7105")));
                options.AddSpotMesh("spot.mesh", mesh =>
                {
                    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                    mesh.AddNode("stage-node", node =>
                    {
                        node.Bind("tcp://127.0.0.1:9000");
                        node.AcceptSpotRoutesFromChannel(
                            "api",
                            routes => routes.UseManualConnections(
                                peers => peers.Connect("tcp://127.0.0.1:7105")));
                    });
                });
            }));

        Assert.Contains("does not enable router capability", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_RequiresDiscoveryOrManualConnections()
    {
        var services = new ServiceCollection();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel(
                    "api",
                    channel => channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7106")));
                options.AddSpotMesh("spot.mesh", mesh =>
                {
                    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                    mesh.AddNode("stage-node", node =>
                    {
                        node.Bind("tcp://127.0.0.1:9000");
                        node.EnableRouter();
                        node.AcceptSpotRoutesFromChannel("api");
                    });
                });
            }));

        Assert.Contains("requires discovery or manual connections", exception.Message, StringComparison.Ordinal);
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
    public void AddZLinkFramework_AllowsStandaloneLocalSpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:9000");
                spot.AddSpotFactory<TestSpot>("stage");
            });
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Single(registration.SpotNodes);
        Assert.Null(registration.SpotDiscovery);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotMeshHasNoUseDiscovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("game.stage", mesh =>
                {
                    mesh.AddNode("stage-node", spot =>
                    {
                        spot.Bind("tcp://127.0.0.1:9000");
                        spot.AddSpotFactory<TestSpot>("stage");
                    });
                });
            }));

        Assert.Contains("requires spotMesh.UseDiscovery", exception.Message, StringComparison.Ordinal);
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
    public void AddZLinkFramework_Throws_When_ActorFactory_Without_SpotNode()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<TestActorFactory>("warrior");
            }));

        Assert.Contains("requires at least one SpotNode", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorManager_Without_SpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(_ => { });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SpotServices_Without_SpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(_ => { });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotManager>());
        Assert.Null(provider.GetService<IZLinkSpotClient>());
        Assert.Null(provider.GetService<IZLinkSpotConnectionManager>());
        Assert.Null(provider.GetService<IZLinkSpotPublisherClient>());
        Assert.Null(provider.GetService<IZLinkSpotMeshPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_SpotServices_When_SpotNode_Exists()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:6200");
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotManager>());
        Assert.NotNull(provider.GetService<IZLinkSpotClient>());
        Assert.NotNull(provider.GetService<IZLinkSpotConnectionManager>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorManager_With_SpotNode_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:6203");
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_ActorManager_When_SpotNode_And_ActorFactory_Exist()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<TestActorFactory>("warrior");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:6201");
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SpotPublisher_Without_PublisherCapability()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:6204");
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotPublisherClient>());
        Assert.Null(provider.GetService<IZLinkSpotMeshPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_SpotPublisher_When_PublisherCapability_Exists()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:6205");
                spot.AttachSpotMeshPublisherClient("game.stage");
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotPublisherClient>());
        Assert.NotNull(provider.GetService<IZLinkSpotMeshPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SessionProxy_Without_BindingStore()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("gateway", routed =>
            {
                routed.Bind("tcp://127.0.0.1:6202");
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSessionProxyFactory>());
        Assert.Null(provider.GetService<IZLinkActorSessionClient>());
    }

    [Fact]
    public void AddZLinkFramework_Throws_When_BindingStore_Without_RouteMesh()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddActorSessionBindingStore<TestActorSessionBindingStore>();
            }));

        Assert.Contains("requires AddRouteMeshChannel", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Allows_SpotRouteResolver_Without_SpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotRouteResolver<TestSpotRouteResolver>();
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<TestSpotRouteResolver>(provider.GetRequiredService<IZLinkSpotRouteResolver>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SpotClient_With_Resolver_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotRouteResolver<TestSpotRouteResolver>();
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotClient>());
    }

    [Fact]
    public void RegistryActorRoutes_Registers_Default_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.UseSpotDiscovery(
                "spot",
                discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind("tcp://127.0.0.1:6202");
            });
            options.UseRegistryActorRoutes("bingo");
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<ZLinkRegistryActorRouteResolver>(
            provider.GetRequiredService<IZLinkActorPlayRouteResolver>());
    }

    [Fact]
    public void RegistrySpotRoutes_Registers_Default_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.UseSpotDiscovery(
                "spot",
                discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind("tcp://127.0.0.1:6202");
            });
            options.UseRegistrySpotRoutes("bingo");
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<ZLinkRegistrySpotRouteResolver>(
            provider.GetRequiredService<IZLinkSpotRouteResolver>());
    }

    [Fact]
    public void RegistryActorSessionBindings_Registers_Default_Service()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("session", routed =>
            {
                routed.Bind("tcp://127.0.0.1:6202");
            });
            options.UseRegistryActorSessionBindings("bingo");
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<ZLinkRegistryActorSessionBindingStore>(
            provider.GetRequiredService<IZLinkActorSessionBindingStore>());
        Assert.NotNull(provider.GetRequiredService<IZLinkSessionProxyFactory>());
        Assert.NotNull(provider.GetRequiredService<IZLinkActorSessionClient>());
    }

    [Fact]
    public void RegistrySpotRoutes_Require_SpotDiscovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("play", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.UseRegistrySpotRoutes("bingo");
            }));

        Assert.Contains("requires AddSpotMesh", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryActorRoutes_Require_Discovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("play", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.UseRegistryActorRoutes("bingo");
            }));

        Assert.Contains("requires UseDiscovery", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryRouteResolvers_Reject_Custom_Duplicate()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseRegistryActorRoutes("bingo");
                options.AddActorPlayRouteResolver<TestActorPlayRouteResolver>();
            }));

        Assert.Contains("already configured", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryActorSessionBindings_Reject_Custom_Duplicate()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseRegistryActorSessionBindings("bingo");
                options.AddActorSessionBindingStore<TestActorSessionBindingStore>();
            }));

        Assert.Contains("already configured", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryActorSessionBindings_Require_Discovery()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("session", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.UseRegistryActorSessionBindings("bingo");
            }));

        Assert.Contains("requires UseDiscovery", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void RegistryRouteResolvers_Require_Explicit_RouterChannel_When_Ambiguous()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.UseSpotDiscovery(
                    "spot",
                    discovery => discovery.Add("tcp://127.0.0.1:5551"));
                options.AddRouteMeshChannel("play-a", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6202");
                });
                options.AddRouteMeshChannel("play-b", routed =>
                {
                    routed.Bind("tcp://127.0.0.1:6203");
                });
                options.UseRegistrySpotRoutes("bingo");
            }));

        Assert.Contains("requires RouterChannelId", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task RouteClient_Throws_ConfigurationException_When_RouteChannel_Missing()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(_ => { });

        using var host = builder.Build();
        await host.StartAsync();

        var client = host.Services.GetRequiredService<IZLinkRouteClient>();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() =>
            client.SendTo(
                    "missing",
                    global::Systems.Zlink.RoutingId.FromString("01"),
                    "ping")
                .Submit().AsTask());

        Assert.Contains("Route channel 'missing' is not registered", exception.Message, StringComparison.Ordinal);
        await host.StopAsync();
    }

    [Fact]
    public async Task ChannelClient_Throws_ConfigurationException_When_ClientCapability_Missing()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(_ => { });

        using var host = builder.Build();
        await host.StartAsync();

        var client = host.Services.GetRequiredService<IZLinkClient>();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() =>
            client.Send("missing", "ping").Submit().AsTask());

        Assert.Contains("Channel client 'missing' is not registered", exception.Message, StringComparison.Ordinal);
        await host.StopAsync();
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
            options.AddActorPlayRouteResolver<TestActorPlayRouteResolver>();
            options.AddSpotRouteResolver<TestSpotRouteResolver>();
            options.AddActorSessionBindingStore<TestActorSessionBindingStore>();
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

            options.AddRouteMeshChannel("gateway", routed =>
            {
                routed.Bind("tcp://127.0.0.1:7301");
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
        Assert.IsType<TestActorPlayRouteResolver>(
            provider.GetRequiredService<IZLinkActorPlayRouteResolver>());
        Assert.IsType<TestSpotRouteResolver>(
            provider.GetRequiredService<IZLinkSpotRouteResolver>());
        Assert.IsType<TestActorSessionBindingStore>(
            provider.GetRequiredService<IZLinkActorSessionBindingStore>());
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
        public TestHeaderSession(IZLinkSessionContext context)
        {
            Context = context;
        }

        public IZLinkSessionContext Context { get; }

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
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId, context));
        }
    }

    private sealed class TestActorPlayRouteResolver : IZLinkActorPlayRouteResolver
    {
        public ValueTask<ZLinkActorRoute> ResolvePlayRouteAsync(
            string actorId,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new ZLinkActorRoute(
                "play",
                global::Systems.Zlink.RoutingId.FromString("01")));
        }
    }

    private sealed class TestSpotRouteResolver : IZLinkSpotRouteResolver
    {
        public ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
            string spotName,
            CancellationToken cancellationToken)
        {
            _ = spotName;
            cancellationToken.ThrowIfCancellationRequested();
            return ResolveSpotRouteAsync(
                global::Systems.Zlink.RoutingId.FromString("02"),
                cancellationToken);
        }

        public ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
            global::Systems.Zlink.RoutingId spotRid,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new ZLinkSpotRoute(
                "spots",
                global::Systems.Zlink.RoutingId.FromString("03"),
                spotRid));
        }
    }

    private sealed class TestActorSessionBindingStore : IZLinkActorSessionBindingStore
    {
        public ValueTask BindSessionAsync(
            ZLinkActorSessionBinding binding,
            CancellationToken cancellationToken)
        {
            _ = binding;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }

        public ValueTask UnbindSessionAsync(
            ZLinkActorSessionUnbind binding,
            CancellationToken cancellationToken)
        {
            _ = binding;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.CompletedTask;
        }

        public ValueTask<ZLinkActorSessionRoute> FindSessionAsync(
            string actorId,
            CancellationToken cancellationToken)
        {
            _ = actorId;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new ZLinkActorSessionRoute(
                global::Systems.Zlink.RoutingId.FromString("04"),
                "binding"));
        }
    }

    private sealed class TestActor(
        string actorId,
        IZLinkActorContext context) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }
}
