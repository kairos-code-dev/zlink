using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;


public sealed class RegistryAndMonitoringTests : RegistrationValidationSupport
{
    [Fact]
    public void RoutedSpotClient_DI_RequiresRoutedSpotEgressCapability()
    {
        var withoutEgress = new ServiceCollection();
        withoutEgress.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("gateway.client", channel =>
            {
                channel.EnableClient(client =>
                    client.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7101")));
            });
        });

        using (var provider = withoutEgress.BuildServiceProvider())
        {
            Assert.Null(provider.GetService<IZLinkRoutedSpotClient>());
        }

        var clientServerEgress = new ServiceCollection();
        clientServerEgress.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("gateway.client", channel =>
            {
                channel.EnableClient(client =>
                    client.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7201")));
                channel.EnableSpotRouteEgress("play.route");
            });
        });

        using (var provider = clientServerEgress.BuildServiceProvider())
        {
            Assert.NotNull(provider.GetService<IZLinkRoutedSpotClient>());
        }

        var routeMeshEgress = new ServiceCollection();
        routeMeshEgress.AddZLinkFramework(options =>
        {
            options.AddRouteMeshChannel("gateway.route", channel =>
            {
                channel.Bind("tcp://127.0.0.1:7301");
                channel.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7201"));
                channel.EnableSpotRouteEgress("play.route");
            });
        });

        using (var provider = routeMeshEgress.BuildServiceProvider())
        {
            Assert.NotNull(provider.GetService<IZLinkRoutedSpotClient>());
        }
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
            options.AddSpotRemoteAddressResolver<TestSpotRemoteAddressResolver>();
            options.ConfigureDispatch(dispatch =>
            {
                dispatch.SpotDispatchMode = ZLinkDispatchMode.Dynamic;
            });
            options.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));


            options.AddClientServerChannel("profile", channel =>
            {
                channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                channel.EnableClient();
                channel.AddRequestHandler<TestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
            });

            options.AddFanoutChannel("profile.events", events =>
            {
                events.EnableSubscriber();
                events.AddPublishHandler<TestPublishHandler, TestPublishedEvent>();
            });

            options.AddStreamNode("stream.node", stream =>
            {
                stream.Bind("tcp://127.0.0.1:9100");
                stream.RegisterSession<TestHeaderSession>();
            });

            options.AddRouteMeshChannel("gateway", routed =>
            {
                routed.Bind("tcp://127.0.0.1:7301");
            });

            options.AddSpotMesh("game.stage", mesh =>
            {
                mesh.UseDiscovery(discovery => discovery.Add("tcp://127.0.0.1:5551"));
                mesh.AddNode("stage-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind("tcp://127.0.0.1:9000");
                });
                spot.AddSpotFactory<TestSpot>();
                spot.AttachClientServerChannelClient("profile");
            });
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var filter = provider.GetRequiredService<TestFilter>();

        Assert.NotNull(filter);
        Assert.IsType<TestSpotRemoteAddressResolver>(
            provider.GetRequiredService<IZLinkSpotRemoteAddressResolver>());
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
                channel.AddRequestHandler<TestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
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
}
