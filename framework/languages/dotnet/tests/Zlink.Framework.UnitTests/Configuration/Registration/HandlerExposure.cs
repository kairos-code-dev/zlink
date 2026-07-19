using System.Reflection;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.UnitTests;

public sealed class HandlerExposureTests : RegistrationValidationSupport
{
    [Fact]
    public async Task RouteClient_Throws_ConfigurationException_When_RouteChannel_Missing()
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(_ => { });

        using var host = builder.Build();
        await host.StartAsync();

        var client = host.Services.GetRequiredService<IZLinkRouteClient>();
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(async () =>
            await client.SendToNode(
                "missing",
                RoutingId.From("01"),
                "ping").SubmitAsync());

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

        var client = host.Services.GetRequiredService<IZLinkChannelClient>();
        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            client.SendToChannel("missing", "ping").TrySubmit());

        Assert.Contains("Channel client 'missing' is not registered", exception.Message, StringComparison.Ordinal);
        await host.StopAsync();
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenServerHasNoBindEndpoint()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options => { options.AddClientServerChannel("profile").EnableServer(""); }));

        Assert.Contains("Channel server bind endpoint must not be empty", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenServerHasNoHandlerExposure()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                {
                    var channel = options.AddClientServerChannel("profile");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                }
            }));

        Assert.Contains("must map a handler group", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenFanoutSubscriberHasNoHandlerExposure()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseTestLocationStore();
                {
                    var channel = options.AddFanoutChannel("profile.events");
                    channel.EnableSubscriber();
                }
            }));

        Assert.Contains("must map a publish handler group", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenMappedHandlerGroupIsUnknown()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                {
                    var channel = options.AddClientServerChannel("profile");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                    channel.AddHandlerGroup("missing-group");
                }
            }));

        Assert.Contains("maps unknown handler group 'missing-group'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenMappedHandlerGroupHasIncompatibleKind()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                {
                    var channel = options.AddClientServerChannel("profile");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                    channel.AddHandlerGroup("validation-publish");
                }
            }));

        Assert.Contains("incompatible handler kind", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsRouteMeshMappedHandlerGroup()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
            {
                var channel = options.AddRouteMeshChannel("backend");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.EnableClient("tcp://127.0.0.1:7102");
                channel.AddHandlerGroup("validation-route");
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var channel = Assert.Single(registration.RouteChannels.Values);

        Assert.Contains("validation-route", channel.HandlerGroups);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteMeshMappedHandlerGroupIsUnknown()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                {
                    var channel = options.AddRouteMeshChannel("backend");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                    channel.EnableClient("tcp://127.0.0.1:7102");
                    channel.AddHandlerGroup("missing-route-group");
                }
            }));

        Assert.Contains("maps unknown handler group 'missing-route-group'", exception.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteMeshMappedHandlerGroupHasIncompatibleKind()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                {
                    var channel = options.AddRouteMeshChannel("backend");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                    channel.EnableClient("tcp://127.0.0.1:7102");
                    channel.AddHandlerGroup("validation-publish");
                }
            }));

        Assert.Contains("incompatible handler kind", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenMappedGroupAndTypedHandlerExposeSameChannelPacket()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                {
                    var channel = options.AddClientServerChannel("profile");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                    channel.AddHandlerGroup("validation-request");
                    channel
                        .AddRequestHandler<AlternateTestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
                }
            }));

        Assert.Contains("duplicate Request handler packet", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSameChannelHandlerIsMappedAndExplicit()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                var channel = options.AddClientServerChannel("profile");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.AddHandlerGroup("validation-request");
                channel.AddRequestHandler<TestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
            }));

        Assert.Contains("duplicate Request handler packet", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenMappedGroupAndTypedHandlerExposeSameRoutePacket()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                {
                    var channel = options.AddRouteMeshChannel("backend");
                    channel.EnableServer("tcp://127.0.0.1:7101");
                    channel.EnableClient("tcp://127.0.0.1:7102");
                    channel.AddHandlerGroup("validation-route");
                    channel.AddRequestHandler<AlternateTestRouteRequestHandler, TestRouteRequest, TestRouteReply>();
                }
            }));

        Assert.Contains("duplicate Request handler packet", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenSameRouteHandlerIsMappedAndExplicit()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                var channel = options.AddRouteMeshChannel("backend");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.AddHandlerGroup("validation-route");
                channel.AddRequestHandler<TestRouteRequestHandler, TestRouteRequest, TestRouteReply>();
            }));

        Assert.Contains("duplicate Request handler packet", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_ChannelAndRouteUseTheSameExplicitPacketIdentityPolicy()
    {
        var channelServices = new ServiceCollection();
        var channelException = Assert.Throws<ZLinkConfigurationException>(() =>
            channelServices.AddZLinkFramework(options =>
            {
                var channel = options.AddClientServerChannel("profile");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.AddRequestHandler<TestChannelRequestHandler, TestChannelRequest, TestChannelReply>("shared");
                channel.AddRequestHandler<AlternateTestChannelRequestHandler, TestChannelRequest, TestChannelReply>(
                    "shared");
            }));

        var routeServices = new ServiceCollection();
        var routeException = Assert.Throws<ZLinkConfigurationException>(() =>
            routeServices.AddZLinkFramework(options =>
            {
                var route = options.AddRouteMeshChannel("backend");
                route.EnableServer("tcp://127.0.0.1:7201");
                route.AddRequestHandler<TestRouteRequestHandler, TestRouteRequest, TestRouteReply>("shared");
                route.AddRequestHandler<AlternateTestRouteRequestHandler, TestRouteRequest, TestRouteReply>("shared");
            }));

        Assert.Contains("Duplicate request handler 'profile:shared'", channelException.Message,
            StringComparison.Ordinal);
        Assert.Contains("Duplicate routed request handler 'backend:shared'", routeException.Message,
            StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsTypedHandlerRegistrationWithoutHandlerGroup()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("profile");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.AddRequestHandler<TestChannelRequestHandler>();
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var channel = Assert.Single(registration.Channels.Values);
        var handler = Assert.Single(channel.RequestHandlers);

        Assert.Empty(channel.HandlerGroups);
        Assert.Equal(typeof(TestChannelRequestHandler), handler.HandlerType);
        Assert.Equal(typeof(TestChannelRequest), handler.MessageType);
        Assert.Equal(typeof(TestChannelReply), handler.ReplyType);
    }

    [Fact]
    public void AddZLinkFramework_Registers_MeshChannel_Handler_With_Constructor_Dependency()
    {
        var services = new ServiceCollection();
        services.AddSingleton<MeshHandlerDependency>();
        services.AddZLinkFramework(options =>
        {
            options.AddRouteMesh("profile")
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From("profile-node"))
                .ChannelName("profile")
                .AddRequestHandler<MeshDependentRequestHandler, TestChannelRequest, TestChannelReply>();
        });

        using var provider = services.BuildServiceProvider();
        using var scope = provider.CreateScope();
        var handler = scope.ServiceProvider.GetRequiredService<MeshDependentRequestHandler>();

        Assert.Same(
            provider.GetRequiredService<MeshHandlerDependency>(),
            handler.Dependency);
    }

    [Fact]
    public void AddZLinkFramework_RouteMeshEnableServer_BindsSharedRouter()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var channel = options.AddRouteMeshChannel("route").EnableServer("tcp://0.0.0.0:5700");
                channel.SetRoutingId(RoutingId.From("route-node"));
                channel.AddRequestHandler<TestRouteRequestHandler>();
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var route = registration.RouteChannels["route"];

        Assert.Equal("tcp://0.0.0.0:5700", route.BindEndpoint);
        Assert.Equal(RoutingId.From("route-node"), route.RoutingId);
        Assert.Empty(route.ManualConnections);
        Assert.Single(route.RequestHandlers);
    }

    [Fact]
    public void AddZLinkFramework_RouteMeshNodeCanEnableBothServerAndClient()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddRouteMeshChannel("route").EnableServer("tcp://0.0.0.0:5700");
                channel.SetRoutingId(RoutingId.From("route-node"));
                channel.EnableClient("tcp://10.0.0.2:5700");
                channel.AddRequestHandler<TestRouteRequestHandler>();
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var route = registration.RouteChannels["route"];

        Assert.Equal("tcp://0.0.0.0:5700", route.BindEndpoint);
        Assert.Equal(RoutingId.From("route-node"), route.RoutingId);
        Assert.False(route.RoutingConfig.EnablePeerProbe);
        Assert.Equal("tcp://10.0.0.2:5700", Assert.Single(route.ManualConnections));
    }

    [Fact]
    public void AddZLinkFramework_AllowsSingleGenericRouteAndPublishHandlerRegistration()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseTestLocationStore();
            {
                var channel = options.AddRouteMeshChannel("backend");
                channel.EnableServer("tcp://127.0.0.1:7101");
                channel.AddRequestHandler<TestRouteRequestHandler>();
            }
            {
                var channel = options.AddFanoutChannel("events");
                channel.EnableSubscriber();
                channel.AddPublishHandler<TestPublishHandler>();
            }
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var route = Assert.Single(registration.RouteChannels.Values);
        var routeHandler = Assert.Single(route.RequestHandlers);
        var fanout = Assert.Single(registration.Channels.Values);
        var publishHandler = Assert.Single(fanout.PublishHandlers);

        Assert.Equal(typeof(TestRouteRequestHandler), routeHandler.HandlerType);
        Assert.Equal(typeof(TestRouteRequest), routeHandler.MessageType);
        Assert.Equal(typeof(TestRouteReply), routeHandler.ReplyType);
        Assert.Equal(typeof(TestPublishHandler), publishHandler.HandlerType);
        Assert.Equal(typeof(TestPublishedEvent), publishHandler.MessageType);
    }

    [Fact]
    public void RemovedSpotEgressClient_PublicSurface_IsRemoved()
    {
        var removedTypes = typeof(IZLinkSpotOutbound).Assembly.GetTypes()
            .Where(static type => type.Namespace == "Zlink.Framework.Contracts.Spots"
                                  && type.Name.Contains("Routed", StringComparison.Ordinal)
                                  && type.Name.Contains("Spot", StringComparison.Ordinal)
                                  && type.Name.Contains("Client", StringComparison.Ordinal));

        Assert.Empty(removedTypes);
    }

    [Fact]
    public void ChannelBuilder_PublicSurface_Matches_HandlerCapabilities()
    {
        var clientServerMethods = PublicInterfaceMethods(typeof(IZLinkClientServerChannelBuilder));
        var routeMeshMethods = PublicInterfaceMethods(typeof(IZLinkRouteMeshChannelBuilder));
        var fanoutMethods = PublicInterfaceMethods(typeof(IZLinkFanoutChannelBuilder));

        Assert.Contains(
            clientServerMethods,
            static method => method.Name == nameof(IZLinkClientServerChannelBuilder.AddHandlerGroup)
                             && method.GetParameters() is [{ ParameterType: var parameterType }]
                             && parameterType == typeof(string));
        Assert.Contains(
            clientServerMethods,
            static method => method.Name == nameof(IZLinkClientServerChannelBuilder.AddSendHandler)
                             && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            clientServerMethods,
            static method => method.Name == nameof(IZLinkClientServerChannelBuilder.AddSendHandler)
                             && method.GetGenericArguments().Length == 2);
        Assert.Contains(
            clientServerMethods,
            static method => method.Name == nameof(IZLinkClientServerChannelBuilder.AddRequestHandler)
                             && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            clientServerMethods,
            static method => method.Name == nameof(IZLinkClientServerChannelBuilder.AddRequestHandler)
                             && method.GetGenericArguments().Length == 3);
        Assert.Contains(
            routeMeshMethods,
            static method => method.Name == nameof(IZLinkRouteMeshChannelBuilder.AddHandlerGroup)
                             && method.GetParameters() is [{ ParameterType: var parameterType }]
                             && parameterType == typeof(string));
        Assert.Contains(
            routeMeshMethods,
            static method => method.Name == nameof(IZLinkRouteMeshChannelBuilder.AddSendHandler)
                             && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            routeMeshMethods,
            static method => method.Name == nameof(IZLinkRouteMeshChannelBuilder.AddSendHandler)
                             && method.GetGenericArguments().Length == 2);
        Assert.Contains(
            routeMeshMethods,
            static method => method.Name == nameof(IZLinkRouteMeshChannelBuilder.AddRequestHandler)
                             && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            routeMeshMethods,
            static method => method.Name == nameof(IZLinkRouteMeshChannelBuilder.AddRequestHandler)
                             && method.GetGenericArguments().Length == 3);
        Assert.Contains(
            fanoutMethods,
            static method => method.Name == nameof(IZLinkFanoutChannelBuilder.AddHandlerGroup)
                             && method.GetParameters() is [{ ParameterType: var parameterType }]
                             && parameterType == typeof(string));
        Assert.Contains(
            fanoutMethods,
            static method => method.Name == nameof(IZLinkFanoutChannelBuilder.AddPublishHandler)
                             && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            fanoutMethods,
            static method => method.Name == nameof(IZLinkFanoutChannelBuilder.AddPublishHandler)
                             && method.GetGenericArguments().Length == 2);
        Assert.DoesNotContain(
            fanoutMethods,
            static method => method.Name is "AddSendHandler" or "AddRequestHandler");

        static IReadOnlyList<MethodInfo> PublicInterfaceMethods(Type type)
        {
            return type.GetInterfaces()
                .Append(type)
                .SelectMany(static interfaceType => interfaceType.GetMethods())
                .ToArray();
        }
    }

    private sealed class MeshHandlerDependency;

    private sealed class MeshDependentRequestHandler(MeshHandlerDependency dependency)
        : IZLinkRequestHandler<TestChannelRequest, TestChannelReply>
    {
        public MeshHandlerDependency Dependency { get; } = dependency;

        public ValueTask<TestChannelReply> HandleAsync(
            TestChannelRequest request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new TestChannelReply(request.Value));
        }
    }
}
