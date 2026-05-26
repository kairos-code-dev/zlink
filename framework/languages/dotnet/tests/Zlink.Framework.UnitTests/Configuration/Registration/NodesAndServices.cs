using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;


public sealed class NodesAndServicesTests : RegistrationValidationSupport
{
    [Fact]
    public void AddZLinkFramework_Throws_WhenStreamNodeRegistersMultipleSessions()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind("tcp://127.0.0.1:9100");
                    stream.RegisterSession<TestHeaderSession>();
                    stream.RegisterSession<TestHeaderSession>();
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
            options.AddSpotMesh("stage-node", mesh =>
            {
                mesh.AddNode("stage-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:9000");
                });
                spot.AddSpotFactory<TestSpot>();
            });
            });
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Single(registration.SpotNodes);
        Assert.NotNull(registration.SpotDiscovery);
        Assert.Empty(registration.SpotDiscovery.Endpoints);
    }

    [Fact]
    public void AddZLinkFramework_AllowsSpotMeshToInheritGlobalDiscovery()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.AddNode("stage-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind("tcp://127.0.0.1:9000");
                    });
                    spot.AddSpotFactory<TestSpot>();
                });
            });
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Empty(registration.SpotDiscovery!.Endpoints);
        Assert.Equal(["tcp://127.0.0.1:5551"], ZLinkFrameworkRegistrationValidator.ResolveSpotDiscoveryEndpoints(registration));
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotFactoryTypeIsDuplicatedAcrossNodes()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("game.stage", mesh =>
                {
                    mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                    mesh.AddNode("stage-node-a", spot =>
                    {
                        spot.EnableRouter(router =>
                        {
                            router.SetRouterBind("tcp://127.0.0.1:6101");
                        });
                        spot.AddSpotFactory<TestSpot>();
                    });
                    mesh.AddNode("stage-node-b", spot =>
                    {
                        spot.EnableRouter(router =>
                        {
                            router.SetRouterBind("tcp://127.0.0.1:6102");
                        });
                        spot.AddSpotFactory<TestSpot>();
                    });
                });
            }));

        Assert.Contains("Duplicate SPOT factory", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotNodeRegistersMultipleEntrySpots()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("game.stage", mesh =>
                {
                    mesh.UseDiscovery(_ => { });
                    mesh.AddNode("stage-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind("tcp://127.0.0.1:6101");
                    });
                    spot.AddEntrySpot<TestEntrySpot>();
                    spot.AddEntrySpot<TestEntrySpot>();
                });
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
        Assert.Null(provider.GetService<IZLinkSpotPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_SpotServices_When_SpotNode_Exists()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("stage-node", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("stage-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:6200");
                });
            });
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotManager>());
        Assert.NotNull(provider.GetService<IZLinkSpotClient>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorManager_With_SpotNode_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("stage-node", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("stage-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:6203");
                });
            });
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
            options.AddSpotMesh("actor-node", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("actor-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:6201");
                });
            });
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void SessionActorDispatch_Does_Not_Require_ActorRemoteAddressResolver()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("actor-node", mesh =>
            {
                mesh.AddNode("actor-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:7302");
                });
            });
            });
            options.AddStreamNode("stream.node", stream =>
            {
                stream.Bind("tcp://127.0.0.1:9100");
                stream.AttachActorGateway("actor-node");
                stream.RegisterSession<TestHeaderSession>();
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();

        Assert.Single(registration.StreamNodes);
        Assert.Empty(registration.RouteChannels);
        Assert.Equal(
            "actor-node",
            registration.StreamNodes["stream.node"].ActorGatewaySpotNodeName);
    }

    [Fact]
    public void AddZLinkFramework_Throws_When_Stream_Attaches_ActorGateway_Node_Without_Router()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                    {
                        spot.EnablePubSub(pubsub =>
                        {
                            pubsub.SetPubBind("tcp://127.0.0.1:7301");
                        });
                    });
                });
                options.AddStreamNode("stream.node", stream =>
                {
                    stream.Bind("tcp://127.0.0.1:9100");
                    stream.AttachActorGateway("actor-node");
                    stream.RegisterSession<TestHeaderSession>();
                });
            }));

        Assert.Contains("does not enable router capability", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Registers_Option_Types_And_Enumerable_Dependencies()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddStreamNode("client.stream", stream =>
            {
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestSessionWithEnumerableHandlers>();
            });
            options.AddSpotMesh("stage-node", mesh =>
            {
                mesh.AddNode("stage-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind("tcp://127.0.0.1:6204");
                    });
                    spot.AddSpotFactory<TestSpot>();
                    spot.AddEntrySpot<TestEntrySpot>();
                });
            });
        });

        Assert.Contains(services, static service => service.ServiceType == typeof(TestSessionWithEnumerableHandlers));
        Assert.Contains(services, static service => service.ServiceType == typeof(TestSpot));
        Assert.Contains(services, static service => service.ServiceType == typeof(TestEntrySpot));

        using var provider = services.BuildServiceProvider();
        Assert.NotEmpty(provider.GetServices<ITestSessionDependencyHandler>());
    }

    [Fact]
    public async Task AddZLinkFramework_Registers_SessionPacketDispatcher_And_Handlers()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddStreamNode("client.stream", stream =>
            {
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestSessionWithPacketDispatcher>();
            });
        });

        using var provider = services.BuildServiceProvider();
        await using var scope = provider.CreateAsyncScope();
        var dispatcher = scope.ServiceProvider.GetRequiredService<IZLinkSessionPacketDispatcher<TestSessionPacketContext>>();
        var context = new TestSessionPacketContext();
        var handled = await dispatcher.TryHandleAsync(
            context,
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.None,
                null,
                "test.session.packet",
                ZlinkStreamMetadata.Empty),
            new Message());
        var unhandled = await dispatcher.TryHandleAsync(
            context,
            new ZlinkStreamHeader(
                ZlinkStreamMessageKind.Send,
                ZlinkStreamCodec.Json,
                ZlinkStreamHeaderFlags.None,
                null,
                "test.unhandled",
                ZlinkStreamMetadata.Empty),
            new Message());

        Assert.True(handled);
        Assert.False(unhandled);
        Assert.Equal(1, context.HandledCount);
    }

    [Fact]
    public void SessionPacketDispatcher_Throws_When_PacketName_Is_Duplicated()
    {
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            new ZLinkSessionPacketDispatcher<DuplicateSessionPacketContext>(
            [
                new DuplicateSessionPacketHandler(),
                new SecondDuplicateSessionPacketHandler()
            ]));

        Assert.Contains("Duplicate session packet handler 'duplicate.session.packet'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SpotPublisher_Without_PublisherCapability()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("stage-node", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("stage-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:6204");
                });
            });
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_SpotPublisher_When_PublisherCapability_Exists()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("stage-node", spot =>
                {
                    spot.EnablePubSub(pubsub =>
                    {
                        pubsub.SetPubBind("tcp://127.0.0.1:6205");
                    });
                    spot.AttachSpotPublisherClient("game.stage");
                });
            });
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_BoundSession_Factory()
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
        Assert.NotNull(provider.GetService<IZLinkBoundSessionFactory>());
    }

    [Fact]
    public void AddZLinkFramework_Allows_SpotRemoteAddressResolver_Without_SpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotRemoteAddressResolver<TestSpotRemoteAddressResolver>();
        });

        using var provider = services.BuildServiceProvider();
        Assert.IsType<TestSpotRemoteAddressResolver>(provider.GetRequiredService<IZLinkSpotRemoteAddressResolver>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_SpotClient_With_Resolver_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotRemoteAddressResolver<TestSpotRemoteAddressResolver>();
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotClient>());
    }

}
