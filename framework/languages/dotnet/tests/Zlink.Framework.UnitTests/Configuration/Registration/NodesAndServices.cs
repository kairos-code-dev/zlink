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
                {
                    var stream = options.AddStreamNode("client.stream");
                    stream.Bind("tcp://127.0.0.1:9100");
                    stream.RegisterSession<TestHeaderSession>();
                    stream.RegisterSession<TestHeaderSession>();

                }
            }));

        Assert.Contains("already has a stream session", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsStandaloneLocalSpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var mesh = options.AddSpotMesh("stage-node");
                {
                    var spot = mesh;
                {
                    var router = spot.EnableRouter("tcp://127.0.0.1:9000");

                }
                spot.AddSpotFactory<TestSpot>();

                }

            }
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Single(registration.SpotNodes);
        Assert.NotNull(registration.SpotDiscovery);
        Assert.Empty(registration.SpotDiscovery.Endpoints);
    }

    [Fact]
    public void AddZLinkFramework_AddSpotMesh_CanConfigureDefaultSpotNode()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("stage-node")
                .EnableRouter("tcp://127.0.0.1:9000")
                .AddSpotFactory<TestSpot>();
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        var node = Assert.Single(registration.SpotNodes.Values);
        Assert.Equal("stage-node", node.SpotNodeName);
        Assert.NotNull(node.Router);
        Assert.Contains(typeof(TestSpot), node.SpotFactories);
    }

    [Fact]
    public void AddZLinkFramework_AddSpotMesh_AllowsMultipleProcessLocalNodes()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotMesh("play-node")
                .EnableRouter("tcp://127.0.0.1:9001")
                .AddSpotFactory<TestSpot>();
            options.AddSpotMesh("session-node")
                .EnableRouter("tcp://127.0.0.1:9002")
                .AddSpotFactory<OtherTestSpot>();
        });

        var registration = services.BuildServiceProvider().GetRequiredService<ZLinkFrameworkRegistration>();
        Assert.Equal(new[] { "play-node", "session-node" }, registration.SpotNodes.Keys);
        Assert.Equal(new[] { "play-node", "session-node" }, registration.SpotDiscoveries.Keys);
        Assert.Null(registration.SpotDiscovery);
        Assert.Equal("play-node", registration.SpotNodes["play-node"].SpotDiscoveryChannelName);
        Assert.Equal("session-node", registration.SpotNodes["session-node"].SpotDiscoveryChannelName);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSpotMeshNameIsDuplicated()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("play-node").EnableRouter("tcp://127.0.0.1:9001");
                options.AddSpotMesh("play-node").EnableRouter("tcp://127.0.0.1:9002");
            }));

        Assert.Contains("Duplicate SPOT mesh channel name 'play-node'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsSpotMeshToInheritGlobalDiscovery()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
            {
                var mesh = options.AddSpotMesh("game.stage");
                {
                    var spot = mesh;
                    {
                        var router = spot.EnableRouter("tcp://127.0.0.1:9000");

                    }
                    spot.AddSpotFactory<TestSpot>();

                }

            }
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
                {
                    var mesh = options.AddSpotMesh("game.stage");
                    mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                    {
                        var spot = mesh;
                        {
                            var router = spot.EnableRouter("tcp://127.0.0.1:6101");

                        }
                        spot.AddSpotFactory<TestSpot>();

                    }
                    {
                        var spot = mesh;
                        {
                            var router = spot.EnableRouter("tcp://127.0.0.1:6102");

                        }
                        spot.AddSpotFactory<TestSpot>();

                    }

                }
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
                {
                    var mesh = options.AddSpotMesh("game.stage");
                    {
                        var spot = mesh;
                    {
                        var router = spot.EnableRouter("tcp://127.0.0.1:6101");

                    }
                    spot.AddEntrySpot<TestEntrySpot>();
                    spot.AddEntrySpot<TestEntrySpot>();

                    }

                }
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
                options.AddSpotMesh("actor-node")
                    .EnableRouter("tcp://127.0.0.1:6102")
                    .AddActorFactory<TestActorFactory>("warrior")
                    .AddActorFactory<TestActorFactory>("warrior");
            }));

        Assert.Contains("Duplicate actor factory 'warrior'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_When_Multiple_SpotNodes_Own_ActorFactories()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("actor-node-a")
                    .EnableRouter("tcp://127.0.0.1:6103")
                    .AddActorFactory<TestActorFactory>("warrior");
                options.AddSpotMesh("actor-node-b")
                    .EnableRouter("tcp://127.0.0.1:6104")
                    .AddActorFactory<TestActorFactory>("mage");
            }));

        Assert.Contains("more than one SpotNode owns actor factories", exception.Message, StringComparison.Ordinal);
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
        Assert.Null(provider.GetService<IZLinkSpotOutbound>());
        Assert.Null(provider.GetService<IZLinkSpotPublisherClient>());
    }

    [Fact]
    public void AddZLinkFramework_Registers_SpotServices_When_SpotNode_Exists()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var mesh = options.AddSpotMesh("stage-node");
                {
                    var spot = mesh;
                {
                    var router = spot.EnableRouter("tcp://127.0.0.1:6200");

                }

                }

            }
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkSpotManager>());
        Assert.NotNull(provider.GetService<IZLinkSpotOutbound>());
    }

    [Fact]
    public void AddZLinkFramework_DoesNot_Register_ActorManager_With_SpotNode_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var mesh = options.AddSpotMesh("stage-node");
                {
                    var spot = mesh;
                {
                    var router = spot.EnableRouter("tcp://127.0.0.1:6203");

                }

                }

            }
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
            options.AddSpotMesh("actor-node")
                .EnableRouter("tcp://127.0.0.1:6201")
                .AddActorFactory<TestActorFactory>("warrior");
        });

        using var provider = services.BuildServiceProvider();
        Assert.NotNull(provider.GetService<IZLinkActorManager>());
    }

    [Fact]
    public void SessionActorDispatch_Registers_Stream_Node_Without_Explicit_Relay_Target()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var mesh = options.AddSpotMesh("actor-node");
                {
                    var spot = mesh;
                {
                    var router = spot.EnableRouter("tcp://127.0.0.1:7302");

                }

                }

            }
            {
                var stream = options.AddStreamNode("stream.node");
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestHeaderSession>();

            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();

        Assert.Single(registration.StreamNodes);
        Assert.Empty(registration.RouteChannels);
    }

    [Fact]
    public void AddZLinkFramework_Registers_Option_Types_And_Enumerable_Dependencies()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var stream = options.AddStreamNode("client.stream");
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestSessionWithEnumerableHandlers>();

            }
            {
                var mesh = options.AddSpotMesh("stage-node");
                {
                    var spot = mesh;
                    {
                        var router = spot.EnableRouter("tcp://127.0.0.1:6204");

                    }
                    spot.AddSpotFactory<TestSpot>();
                    spot.AddEntrySpot<TestEntrySpot>();

                }

            }
        });

        Assert.DoesNotContain(services, static service => service.ServiceType == typeof(TestSessionWithEnumerableHandlers));
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
            {
                var stream = options.AddStreamNode("client.stream");
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestSessionWithPacketDispatcher>();

            }
        });

        using var provider = services.BuildServiceProvider();
        await using var scope = provider.CreateAsyncScope();
        var dispatcher = scope.ServiceProvider.GetRequiredService<IZLinkSessionPacketDispatcher<TestSessionPacketContext>>();
        var context = new TestSessionPacketContext();
        var handled = await dispatcher.TryHandleAsync(
            context,
            new ZLinkSessionDispatchContext("test.session.packet"),
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(new object()));
        var unhandled = await dispatcher.TryHandleAsync(
            context,
            new ZLinkSessionDispatchContext("test.unhandled"),
            Zlink.Framework.Contracts.Messaging.ZLinkMessage.From(new object()));

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
    public void AddZLinkFramework_Registers_SpotPublisher_Client()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var mesh = options.AddSpotMesh("stage-node");
                {
                    var spot = mesh;
                {
                    var router = spot.EnableRouter("tcp://127.0.0.1:6204");

                }

                }

            }
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
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
            {
                var routed = options.AddRouteMesh("gateway");
                routed.EnableServer("tcp://127.0.0.1:6202");

            }
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
    public void AddZLinkFramework_DoesNot_Register_SpotOutbound_With_Resolver_Only()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotRemoteAddressResolver<TestSpotRemoteAddressResolver>();
        });

        using var provider = services.BuildServiceProvider();
        Assert.Null(provider.GetService<IZLinkSpotOutbound>());
    }

}
