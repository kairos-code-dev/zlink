using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;


public sealed class NodesAndServicesTests : RegistrationValidationSupport
{
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
    public void SessionActorDispatch_Does_Not_Require_ActorRemoteAddressResolver()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind("tcp://127.0.0.1:7301");
            });
            options.AddStreamNode("stream.node", stream =>
            {
                stream.Bind("tcp://127.0.0.1:9100");
                stream.AttachActorGateway("actor-node");
                stream.AddHeaderSession<TestHeaderSession>();
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
