using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.Protobuf;

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
            options.AddSpotRouteRefResolver<TestSpotRouteRefResolver>();
            {
                var dispatch = options.ConfigureDispatch();
                dispatch.SpotDispatchMode = ZLinkDispatchMode.Dynamic;
            }
            options.UseInMemoryLocationStores();

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

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var filter = provider.GetRequiredService<TestFilter>();

        Assert.NotNull(filter);
        Assert.IsType<TestSpotRouteRefResolver>(
            provider.GetRequiredService<IZLinkSpotRouteRefResolver>());
        Assert.Equal(TimeSpan.FromSeconds(5), registration.DefaultRequestTimeout);
        Assert.Equal(TimeSpan.FromMilliseconds(1000), registration.DefaultSocketSendTimeout);
        Assert.Contains("application/x-protobuf", registration.Codecs.Serializers.Keys);
        Assert.Equal(ZLinkDispatchMode.Dynamic, registration.DispatchOptions.SpotDispatchMode);
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
}