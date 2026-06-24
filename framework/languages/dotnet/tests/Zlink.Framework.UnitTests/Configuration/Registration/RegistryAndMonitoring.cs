using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;

namespace Zlink.Framework.UnitTests;


public sealed class RegistryAndMonitoringTests : RegistrationValidationSupport
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
                var channel = options.AddRouteMesh("gateway.route");
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
            services.AddZLinkFramework(options =>
            {
                options.AddFanoutChannel("profile").EnablePublisher("");
            }));

        Assert.Contains("Channel publisher bind endpoint must not be empty", exception.Message, StringComparison.Ordinal);
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
            options.AddSpotRemoteAddressResolver<TestSpotRemoteAddressResolver>();
            {
                var dispatch = options.ConfigureDispatch();
                dispatch.SpotDispatchMode = ZLinkDispatchMode.Dynamic;

            }
            options.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");


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
                var routed = options.AddRouteMesh("gateway");
                routed.EnableServer("tcp://127.0.0.1:7301");

            }

            {
                var mesh = options.AddSpotMesh("game.stage");
                mesh.UseDiscovery().AddRegistryEndpoint("tcp://127.0.0.1:5551");
                {
                    var spot = mesh;
                {
                    var router = spot.EnableRouter("tcp://127.0.0.1:9000");

                }
                spot.AddSpotFactory<TestSpot>();
                }

            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var filter = provider.GetRequiredService<TestFilter>();

        Assert.NotNull(filter);
        Assert.IsType<TestSpotRemoteAddressResolver>(
            provider.GetRequiredService<IZLinkSpotRemoteAddressResolver>());
        Assert.Equal(TimeSpan.FromSeconds(5), registration.DefaultRequestTimeout);
        Assert.Equal(TimeSpan.FromMilliseconds(1000), registration.DefaultSocketSendTimeout);
        Assert.Contains("application/x-protobuf", registration.Codecs.Serializers.Keys);
        Assert.Equal(ZLinkDispatchMode.Dynamic, registration.DispatchOptions.SpotDispatchMode);
        Assert.NotNull(registration.Discovery);
        Assert.NotNull(registration.SpotDiscovery);
        Assert.Contains("profile", registration.Channels.Keys);
        Assert.Contains("stream.node", registration.StreamNodes.Keys);
        Assert.Contains("stage-node", registration.SpotNodes.Keys);
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
            options.AddRouteMesh("route")
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
}
