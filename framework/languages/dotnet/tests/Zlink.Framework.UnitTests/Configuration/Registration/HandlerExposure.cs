using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Systems.Zlink.Stream.Connector.Contracts;
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
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() =>
            client.Send(
                    "missing",
                    global::Systems.Zlink.RoutingId.From("01"),
                    "ping")
                .Async().AsTask());

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
        var exception = await Assert.ThrowsAsync<ZLinkConfigurationException>(() =>
            client.SendToChannel("missing", "ping").Async().AsTask());

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
    public void AddZLinkFramework_Throws_WhenServerHasNoHandlerExposure()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("profile", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                });
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
                options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
                options.AddFanoutChannel("profile.events", channel =>
                {
                    channel.EnableSubscriber();
                });
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
                options.AddClientServerChannel("profile", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                    channel.AddHandlerGroup("missing-group");
                });
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
                options.AddClientServerChannel("profile", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                    channel.AddHandlerGroup("validation-publish");
                });
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
            options.AddRouteMeshChannel("backend", channel =>
            {
                channel.Bind("tcp://127.0.0.1:7101");
                channel.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7102"));
                channel.AddHandlerGroup("validation-route");
            });
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
                options.AddRouteMeshChannel("backend", channel =>
                {
                    channel.Bind("tcp://127.0.0.1:7101");
                    channel.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7102"));
                    channel.AddHandlerGroup("missing-route-group");
                });
            }));

        Assert.Contains("maps unknown handler group 'missing-route-group'", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_Throws_WhenRouteMeshMappedHandlerGroupHasIncompatibleKind()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                options.AddRouteMeshChannel("backend", channel =>
                {
                    channel.Bind("tcp://127.0.0.1:7101");
                    channel.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7102"));
                    channel.AddHandlerGroup("validation-publish");
                });
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
                options.AddClientServerChannel("profile", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                    channel.AddHandlerGroup("validation-request");
                    channel.AddRequestHandler<AlternateTestChannelRequestHandler, TestChannelRequest, TestChannelReply>();
                });
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
                options.AddRouteMeshChannel("backend", channel =>
                {
                    channel.Bind("tcp://127.0.0.1:7101");
                    channel.UseManualConnections(peers => peers.Connect("tcp://127.0.0.1:7102"));
                    channel.AddHandlerGroup("validation-route");
                    channel.AddRequestHandler<AlternateTestRouteRequestHandler, TestRouteRequest, TestRouteReply>();
                });
            }));

        Assert.Contains("duplicate Request handler packet", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_AllowsTypedHandlerRegistrationWithoutHandlerGroup()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("profile", channel =>
            {
                channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                channel.AddRequestHandler<TestChannelRequestHandler>();
            });
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
    public void AddZLinkFramework_AllowsDealerMeshSendAndRequestHandlers()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
            options.AddDealerMeshChannel("mesh", channel =>
            {
                channel.EnableClient();
                channel.AddSendHandler<AlternateTestSendHandler, TestSendMessage>();
                channel.AddRequestHandler<TestChannelRequestHandler>();
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var channel = Assert.Single(registration.Channels.Values);

        Assert.Single(channel.SendHandlers);
        Assert.Single(channel.RequestHandlers);
        Assert.Equal(typeof(AlternateTestSendHandler), channel.SendHandlers[0].HandlerType);
        Assert.Equal(typeof(TestChannelRequestHandler), channel.RequestHandlers[0].HandlerType);
    }

    [Fact]
    public void AddZLinkFramework_RejectsDealerMeshPublishHandlerGroup()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
                options.AddHandlersFromAssemblyOf<RegistrationValidationSupport>();
                options.AddDealerMeshChannel("mesh", channel =>
                {
                    channel.EnableClient();
                    channel.AddHandlerGroup("validation-publish");
                });
            }));

        Assert.Contains("cannot expose publish handlers", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void AddZLinkFramework_RegistersDealerMeshHandlersForDependencyInjection()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
            options.AddDealerMeshChannel("mesh", channel =>
            {
                channel.EnableClient();
                channel.AddRequestHandler<TestChannelRequestHandler>();
            });
        });

        using var provider = services.BuildServiceProvider();

        Assert.NotNull(provider.GetService<TestChannelRequestHandler>());
    }

    [Fact]
    public void AddZLinkFramework_DealerMeshEnableServer_BindsSharedDealerWithoutPeerSource()
    {
        var services = new ServiceCollection();

        // dealer mesh 를 server 로만(bind + handler) — discovery/connect 없이도 유효해야 한다.
        services.AddZLinkFramework(options =>
        {
            options.AddDealerMeshChannel("mesh", channel =>
            {
                channel.EnableServer(server => server.Bind("tcp://0.0.0.0:5600"));
                channel.AddRequestHandler<TestChannelRequestHandler>();
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var channel = registration.Channels["mesh"];

        // server·client 가 공유하는 DEALER(Client registration)에 bind 가 기록되고,
        // ROUTER(registration.Server)는 만들어지지 않는다.
        Assert.NotNull(channel.Client);
        Assert.Equal("tcp://0.0.0.0:5600", channel.Client!.BindEndpoint);
        Assert.Null(channel.Server);
        Assert.Single(channel.RequestHandlers);
        Assert.NotNull(provider.GetService<TestChannelRequestHandler>());
    }

    [Fact]
    public void AddZLinkFramework_DealerMeshNodeCanEnableBothServerAndClient()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddDealerMeshChannel("mesh", channel =>
            {
                channel.EnableServer(server => server.Bind("tcp://0.0.0.0:5600"));   // 받는다(제공)
                channel.EnableClient(client => client.UseManualConnections(
                    peers => peers.Connect("tcp://10.0.0.2:5600")));                  // 호출한다(소비)
                channel.AddRequestHandler<TestChannelRequestHandler>();
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var channel = registration.Channels["mesh"];

        // 같은 DEALER 하나가 bind(제공)와 connect(소비)를 모두 가진다.
        Assert.NotNull(channel.Client);
        Assert.Equal("tcp://0.0.0.0:5600", channel.Client!.BindEndpoint);
        Assert.Single(channel.Client.ManualConnections);
        Assert.Null(channel.Server);
    }


    [Fact]
    public void AddZLinkFramework_RouteMeshEnableServer_BindsSharedRouterWithDiscovery()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("route", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind("tcp://0.0.0.0:5700");
                    server.ConfigureRouting(route => route.RoutingId = RoutingId.From("route-node"));
                });
                channel.AddRequestHandler<TestRouteRequestHandler>();
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var route = registration.RouteChannels["route"];

        Assert.Equal("tcp://0.0.0.0:5700", route.BindEndpoint);
        Assert.Equal(RoutingId.From("route-node"), route.RoutingConfig.RoutingId);
        Assert.Empty(route.ManualConnections);
        Assert.Single(route.RequestHandlers);
    }

    [Fact]
    public void AddZLinkFramework_RouteMeshNodeCanEnableBothServerAndClient()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.AddRouteMeshChannel("route", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind("tcp://0.0.0.0:5700");
                    server.ConfigureRouting(route => route.RoutingId = RoutingId.From("route-node"));
                });
                channel.EnableClient(client =>
                {
                    client.ConfigureRouting(route => route.ProbeRouterOnConnect = true);
                    client.UseManualConnections(peers => peers.Connect("tcp://10.0.0.2:5700"));
                });
                channel.AddRequestHandler<TestRouteRequestHandler>();
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var route = registration.RouteChannels["route"];

        Assert.Equal("tcp://0.0.0.0:5700", route.BindEndpoint);
        Assert.Equal(RoutingId.From("route-node"), route.RoutingConfig.RoutingId);
        Assert.True(route.RoutingConfig.EnablePeerProbe);
        Assert.Equal("tcp://10.0.0.2:5700", Assert.Single(route.ManualConnections));
    }
    [Fact]
    public void AddZLinkFramework_AllowsSingleGenericRouteAndPublishHandlerRegistration()
    {
        var services = new ServiceCollection();

        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.AddRegistryEndpoint("tcp://127.0.0.1:5551"));
            options.AddRouteMeshChannel("backend", channel =>
            {
                channel.Bind("tcp://127.0.0.1:7101");
                channel.AddRequestHandler<TestRouteRequestHandler>();
            });
            options.AddFanoutChannel("events", channel =>
            {
                channel.EnableSubscriber();
                channel.AddPublishHandler<TestPublishHandler>();
            });
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
    public void AddZLinkFramework_Throws_WhenClientServerSpotRouteEgressHasNoClient()
    {
        var services = new ServiceCollection();

        var exception = Assert.Throws<ZLinkConfigurationException>(() =>
            services.AddZLinkFramework(options =>
            {
                options.AddClientServerChannel("gateway", channel =>
                {
                    channel.EnableServer(server => server.Bind("tcp://127.0.0.1:7101"));
                    channel.EnableSpotRouteEgress("play.route");
                });
            }));

        Assert.Contains("routed SPOT egress requires client capability", exception.Message, StringComparison.Ordinal);
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
    public void ChannelBuilder_PublicSurface_Matches_HandlerAndEgressCapabilities()
    {
        var clientServerMethods = PublicInterfaceMethods(typeof(IZLinkClientServerChannelBuilder));
        var routeMeshMethods = PublicInterfaceMethods(typeof(IZLinkRouteMeshChannelBuilder));
        var fanoutMethods = PublicInterfaceMethods(typeof(IZLinkFanoutChannelBuilder));
        var dealerMeshMethods = PublicInterfaceMethods(typeof(IZLinkDealerMeshChannelBuilder));

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
            clientServerMethods,
            static method => method.Name == nameof(IZLinkClientServerChannelBuilder.EnableSpotRouteEgress)
                && method.GetParameters() is [{ ParameterType: var parameterType }]
                && parameterType == typeof(string));

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
            routeMeshMethods,
            static method => method.Name == nameof(IZLinkRouteMeshChannelBuilder.EnableSpotRouteEgress)
                && method.GetParameters() is [{ ParameterType: var parameterType }]
                && parameterType == typeof(string));

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
            static method => method.Name is "AddSendHandler" or "AddRequestHandler" or "EnableSpotRouteEgress");

        Assert.Contains(
            dealerMeshMethods,
            static method => method.Name == nameof(IZLinkDealerMeshChannelBuilder.AddHandlerGroup)
                && method.GetParameters() is [{ ParameterType: var parameterType }]
                && parameterType == typeof(string));
        Assert.Contains(
            dealerMeshMethods,
            static method => method.Name == nameof(IZLinkDealerMeshChannelBuilder.AddSendHandler)
                && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            dealerMeshMethods,
            static method => method.Name == nameof(IZLinkDealerMeshChannelBuilder.AddSendHandler)
                && method.GetGenericArguments().Length == 2);
        Assert.Contains(
            dealerMeshMethods,
            static method => method.Name == nameof(IZLinkDealerMeshChannelBuilder.AddRequestHandler)
                && method.GetGenericArguments().Length == 1);
        Assert.Contains(
            dealerMeshMethods,
            static method => method.Name == nameof(IZLinkDealerMeshChannelBuilder.AddRequestHandler)
                && method.GetGenericArguments().Length == 3);
        Assert.DoesNotContain(
            dealerMeshMethods,
            static method => method.Name is "AddPublishHandler" or "EnableSpotRouteEgress");

        static IReadOnlyList<System.Reflection.MethodInfo> PublicInterfaceMethods(Type type)
        {
            return type.GetInterfaces()
                .Append(type)
                .SelectMany(static interfaceType => interfaceType.GetMethods())
                .ToArray();
        }
    }

}
