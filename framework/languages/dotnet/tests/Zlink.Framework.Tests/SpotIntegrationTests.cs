using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Tests;

public sealed partial class SpotIntegrationTests
{
    private static readonly TimeSpan PollingInterval = TimeSpan.FromMilliseconds(150);

    [Fact]
    public async Task SpotManager_Create_List_Remove_And_Publish_Work_Through_FrameworkRuntime()
    {
        var ordersServer = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        var host = await CreateHostAsync(ordersServer, spotNode);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var events = host.Services.GetRequiredService<SpotEventsRecorder>();
            var orders = host.Services.GetRequiredService<OrdersRecorder>();

            var first = await manager.CreateAsync("stage");

            await RetryAsync(
                () => events.Initialized.Count >= 1
                    && orders.ReceivedScopes.Count >= 1,
                TimeSpan.FromSeconds(5));

            Assert.True(first.Created);

            var firstInfo = await manager.GetAsync(first.SpotRid);
            Assert.Equal("stage", firstInfo?.SpotName);

            var listed = await manager.ListAsync();
            Assert.Single(listed);

            Assert.Contains(events.ScopeId(first.SpotRid), orders.ReceivedScopes);

            Assert.True(await manager.RemoveAsync(first.SpotRid));
            Assert.Contains(first.SpotRid, events.Closing);
            Assert.Null(await manager.GetAsync(first.SpotRid));
            Assert.Empty(await manager.ListAsync());

            var firstScope = events.ScopeId(first.SpotRid);
            var second = await manager.CreateAsync("stage");
            await RetryAsync(
                () => events.Initialized.Count >= 2
                    && orders.ReceivedScopes.Count >= 2,
                TimeSpan.FromSeconds(5));
            Assert.True(second.Created);
            Assert.NotEqual(first.SpotRid, second.SpotRid);
            Assert.NotEqual(firstScope, events.ScopeId(second.SpotRid));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_CreateAsync_Passes_Empty_CreatePayload_To_OnCreate()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var recorder = host.Services.GetRequiredService<SpotCreatePayloadRecorder>();

            var created = await manager.CreateAsync("payload-stage");

            Assert.True(created.Created);
            var payload = Assert.Single(recorder.Payloads);
            Assert.Empty(payload);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_GetOrCreateAsync_Initializes_Once_With_First_CreatePayload()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var recorder = host.Services.GetRequiredService<SpotCreatePayloadRecorder>();
            recorder.BlockCreate();
            var spotRid = RoutingId.FromBytes(Encoding.UTF8.GetBytes("payload-room"));
            using var firstA = Message.FromString("first-a");
            using var firstB = Message.FromString("first-b");
            using var second = Message.FromString("second");

            var first = manager.GetOrCreateAsync(
                "payload-stage",
                spotRid,
                [firstA, firstB]).AsTask();
            await recorder.WaitCreateEnteredAsync();
            var secondResult = manager.GetOrCreateAsync(
                "payload-stage",
                spotRid,
                [second]).AsTask();
            recorder.ReleaseCreate();

            var results = await Task.WhenAll(first, secondResult);

            Assert.Single(results, static result => result.Created);
            Assert.Single(results, static result => !result.Created);
            Assert.All(results, result => Assert.Equal(spotRid, result.SpotRid));
            var payload = Assert.Single(recorder.Payloads);
            Assert.Equal(["first-a", "first-b"], payload);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task SpotManager_GetOrCreateAsync_Rejects_SpotName_Mismatch()
    {
        var host = await CreatePayloadHostAsync(GetFreeTcpEndpoint());
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var spotRid = RoutingId.FromBytes(Encoding.UTF8.GetBytes("payload-room-2"));

            _ = await manager.GetOrCreateAsync("payload-stage", spotRid);
            var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(
                () => manager.GetOrCreateAsync("other-payload-stage", spotRid).AsTask());

            Assert.Equal(ZLinkFrameworkErrorKind.SpotTypeMismatch, error.Kind);
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task Spot_Publish_Timer_And_Remove_Stop_Callbacks_Work()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.stage.timer.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        publisherBuilder.Services.AddScoped<SpotHeartbeatTimerHandler>();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("publisher-node", spot =>
            {
                spot.Bind(publisherNodeEndpoint);
                spot.EnablePubSub();
                spot.AddSpotFactory<PublishingStageSpot>("publisher-stage");
            });
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<LocalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub(pubsub =>
                {
                    pubsub.UseManualConnections(connections =>
                        connections.Connect(publisherNodeEndpoint));
                });
                spot.AddSpotFactory<LocalSubscriberStageSpot>("subscriber-stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var publisherHost = publisherBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();

        await registryHost.StartAsync();
        await publisherHost.StartAsync();
        await subscriberHost.StartAsync();

        var publisherManager = publisherHost.Services.GetRequiredService<IZLinkSpotManager>();
        var subscriberManager = subscriberHost.Services.GetRequiredService<IZLinkSpotManager>();
        var publisherRecorder = publisherHost.Services.GetRequiredService<SpotLifecycleRecorder>();

        _ = await subscriberManager.CreateAsync("subscriber-stage");
        var created = await publisherManager.CreateAsync("publisher-stage");

        await RetryAsync(
            () => publisherRecorder.TickCount >= 2,
            TimeSpan.FromSeconds(10));

        var ticksBeforeRemove = publisherRecorder.TickCount;

        Assert.True(await publisherManager.RemoveAsync(created.SpotRid));
        await Task.Delay(300);
        Assert.Equal(ticksBeforeRemove, publisherRecorder.TickCount);

        await subscriberHost.StopAsync();
        await publisherHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task RegistrySpotRoutes_Resolves_Created_Spot_By_Name()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.registry-route.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeChannelEndpoint);
            });
            options.UseRegistrySpotRoutes("registry-route");
            options.AddSpotNode("registry-route-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.AddSpotFactory<LocalSubscriberStageSpot>("registry-stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRouteResolver>();

        var created = await manager.CreateAsync("registry-stage");
        var route = await resolver.ResolveSpotRouteAsync(
            "registry-stage",
            CancellationToken.None);

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal(created.SpotRid, route.SpotRid);

        Assert.True(await manager.RemoveAsync(created.SpotRid));
        var error = await Assert.ThrowsAsync<ZLinkFrameworkException>(() =>
            resolver.ResolveSpotRouteAsync("registry-stage", CancellationToken.None)
                .AsTask());
        Assert.Equal(ZLinkFrameworkErrorKind.SpotRouteNotFound, error.Kind);

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task RegistrySpotRoutes_Resolves_Created_Spot_By_Rid()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"game.registry-route-rid.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.UseSpotDiscovery(spotChannel, discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeChannelEndpoint);
            });
            options.UseRegistrySpotRoutes("registry-route-rid");
            options.AddSpotNode("registry-route-rid-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.AddSpotFactory<LocalSubscriberStageSpot>("registry-stage-rid");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRouteResolver>();

        var created = await manager.CreateAsync("registry-stage-rid");
        var route = await RetryAsync(
            () => resolver.ResolveSpotRouteAsync(created.SpotRid, CancellationToken.None).AsTask(),
            static result => result.SpotRid.ToString().Length > 0,
            TimeSpan.FromSeconds(5));

        Assert.Equal("play", route.RouterChannelId);
        Assert.Equal(created.SpotRid, route.SpotRid);

        Assert.True(await manager.RemoveAsync(created.SpotRid));

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task AcceptSpotRoutesFromChannel_ClientServer_ManualConnections_AreApplied()
    {
        var channelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(channelEndpoint);
                    server.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromString("aabbcc01");
                    });
                });
            });
            options.UseSpotDiscovery("spot.route.client-server", _ => { });
            options.AddSpotNode("route-target-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter();
                spot.AcceptSpotRoutesFromChannel(
                    "api",
                    routes => routes.UseManualConnections(
                        peers => peers.Connect(channelEndpoint)));
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "api");
        var peer = Assert.Single(nodeRuntime.Node.PeersSnapshot(), peer => peer.ChannelName == "api");
        Assert.Equal(ZLinkSpotPeerKind.RouterChannel, peer.Kind);
        Assert.Equal(ZLinkSpotPeerSource.Manual, peer.Source);

        await host.StopAsync();
    }

    [Fact]
    public async Task AcceptSpotRoutesFromChannel_RouteMesh_ManualConnections_AreApplied()
    {
        var routeEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(_ => { });
            options.AddRouteMeshChannel("play", routed =>
            {
                routed.Bind(routeEndpoint);
                routed.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.FromString("aabbcc02");
                });
            });
            options.UseSpotDiscovery("spot.route.mesh", _ => { });
            options.AddSpotNode("route-target-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter();
                spot.AcceptSpotRoutesFromChannel(
                    "play",
                    routes => routes.UseManualConnections(
                        peers => peers.Connect(routeEndpoint)));
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play");
        var peer = Assert.Single(nodeRuntime.Node.PeersSnapshot(), peer => peer.ChannelName == "play");
        Assert.Equal(ZLinkSpotPeerKind.RouterChannel, peer.Kind);
        Assert.Equal(ZLinkSpotPeerSource.Manual, peer.Source);

        await host.StopAsync();
    }

    [Fact]
    public async Task AddSpotNode_AcceptSpotRoutesFromChannel_ClientServer_AllowsRouterSendToSpot()
    {
        await VerifyAcceptedRouteChannelSendToSpotAsync(
            SpotRouteTransportKind.ClientServer,
            "api");
    }

    [Fact]
    public async Task AddSpotNode_AcceptSpotRoutesFromChannel_RouteMesh_AllowsRouterSendToSpot()
    {
        await VerifyAcceptedRouteChannelSendToSpotAsync(
            SpotRouteTransportKind.RouteMesh,
            "play");
    }

    [Fact]
    public async Task RegistrySpotRoutes_RequestSend_By_Name()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"spot.registry.request-send.{Guid.NewGuid():N}";

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddSingleton<SpotRouteTransportRecorder>();
        frameworkBuilder.Services.AddScoped<SpotRouteTargetCommandHandler>();
        frameworkBuilder.Services.AddScoped<SpotRouteTargetRequestHandler>();
        frameworkBuilder.Services.AddScoped<SpotRouteSendCallerHandler>();
        frameworkBuilder.Services.AddScoped<SpotRouteRequestCallerHandler>();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.UseSpotDiscovery(spotChannel, discovery => discovery.Add(registryRouterEndpoint));
            options.UseRegistrySpotRoutes("spot-registry-request-send");
            options.AddRouteMeshChannel("play", route =>
            {
                route.Bind(routeChannelEndpoint);
                route.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.FromBytes(
                        Encoding.UTF8.GetBytes("registry-play-route"));
                });
            });
            options.AddSpotNode("route-target-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter(router =>
                {
                    router.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromBytes(
                            Encoding.UTF8.GetBytes("registry-target-node"));
                    });
                });
                spot.AcceptSpotRoutesFromChannel(
                    "play",
                    routes => routes.UseManualConnections(
                        peers => peers.Connect(routeChannelEndpoint)));
                spot.AddEntrySpot<SpotRouteCallerEntrySpot>();
                spot.AddSpotFactory<SpotRouteTargetSpot>("route-target");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();
        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var runtime = frameworkHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRouteResolver>();
        var recorder = frameworkHost.Services.GetRequiredService<SpotRouteTransportRecorder>();
        var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
        _ = await manager.CreateAsync("route-target");
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play");
        var route = await RetryAsync(
            () => resolver.ResolveSpotRouteAsync("route-target", CancellationToken.None).AsTask(),
            static result => result.SpotRid.Size > 0,
            TimeSpan.FromSeconds(5));
        Assert.Equal(nodeRuntime.Node.RoutingId, route.TargetNodeRid);

        await RetryAsync(
            async () =>
            {
                var header = ZLinkClientCallCodec.CreateEnvelope(
                    ZLinkMessageKind.Request,
                    route.RouterChannelId,
                    ZLinkMessageNameResolver.ResolveFromType(typeof(SpotRouteTargetRequest))
                        ?? throw new InvalidOperationException("Message name is required."),
                    TimeSpan.FromMilliseconds(500));
                var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                    header,
                    new SpotRouteTargetRequest("registry-request"));
                var reply = await runtime.RequestSpotViaRouterChannelAsync(
                        route.RouterChannelId,
                        route.TargetNodeRid,
                        route.SpotRid,
                        parts,
                        TimeSpan.FromMilliseconds(500),
                        CancellationToken.None)
                    .ConfigureAwait(false);
                try
                {
                    var decoded = ZLinkClientCallCodec.DecodeEnvelopeReply<SpotRouteTargetReply>(
                        reply,
                        "SPOT registry route reply is empty.",
                        "SPOT registry route request failed.");
                    return decoded.Value == "reply:registry-request"
                        && recorder.Requests.Contains("registry-request");
                }
                finally
                {
                    foreach (var item in reply)
                    {
                        item.Dispose();
                    }
                }
            },
            static result => result,
            TimeSpan.FromSeconds(5));

        await RetryAsync(
            async () =>
            {
                var header = ZLinkClientCallCodec.CreateEnvelope(
                    ZLinkMessageKind.Command,
                    route.RouterChannelId,
                    ZLinkMessageNameResolver.ResolveFromType(typeof(SpotRouteTargetCommand))
                        ?? throw new InvalidOperationException("Message name is required."));
                var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                    header,
                    new SpotRouteTargetCommand("registry-send"));
                await runtime.SendSpotViaRouterChannelAsync(
                        route.RouterChannelId,
                        route.TargetNodeRid,
                        route.SpotRid,
                        parts,
                        CancellationToken.None)
                    .ConfigureAwait(false);
                return recorder.Commands.Contains("registry-send");
            },
            static result => result,
            TimeSpan.FromSeconds(5));

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public void AcceptSpotRoutesFromChannel_DiscoveryConnections_AreApplied()
    {
        var channelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();

        var services = new ServiceCollection();
        services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add("tcp://127.0.0.1:5551");
            });
            options.UseSpotDiscovery("spot.route.discovery", _ => { });
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(channelEndpoint);
                    server.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromString("aabbcc03");
                    });
                });
            });
            options.AddSpotNode("route-target-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter();
                spot.AcceptSpotRoutesFromChannel("api");
            });
        });

        using var provider = services.BuildServiceProvider();
        var registration = provider.GetRequiredService<ZLinkFrameworkRegistration>();
        var node = registration.SpotNodes["route-target-node"];
        var acceptance = Assert.Single(node.AcceptedSpotRouteChannels.Values);

        Assert.Equal("api", acceptance.ChannelName);
        Assert.Empty(acceptance.ManualConnections);
        Assert.Contains("tcp://127.0.0.1:5551", registration.Discovery?.Endpoints ?? []);
    }

    [Fact]
    public async Task SendSpot_UsesRouterChannelIdTransport()
    {
        var channelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var host = await CreateSpotRouteTransportHostAsync(
            SpotRouteTransportKind.ClientServer,
            "api",
            channelEndpoint,
            spotNodeEndpoint);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var resolver = host.Services.GetRequiredService<FixedSpotRouteResolver>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            var target = await manager.GetOrCreateAsync(
                "route-target",
                RoutingId.FromBytes(Encoding.UTF8.GetBytes("spotapi1")));
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, "api");
            resolver.Configure("api", nodeRuntime.Node.RoutingId, target.SpotRid);

            await RetryAsync(
                async () =>
                {
                    await InvokeEntrySpotPacketAsync(
                        runtime,
                        "route-target-node",
                        "api",
                        new SpotRouteSendCallerCommand("send-via-api"));
                    return recorder.Commands.Contains("send-via-api");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task RequestSpot_UsesRouterChannelIdTransport()
    {
        var routeEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var host = await CreateSpotRouteTransportHostAsync(
            SpotRouteTransportKind.ClientServer,
            "api",
            routeEndpoint,
            spotNodeEndpoint);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var resolver = host.Services.GetRequiredService<FixedSpotRouteResolver>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            var target = await manager.GetOrCreateAsync(
                "route-target",
                RoutingId.FromBytes(Encoding.UTF8.GetBytes("spotapi2")));
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, "api");
            resolver.Configure("api", nodeRuntime.Node.RoutingId, target.SpotRid);

            await RetryAsync(
                async () =>
                {
                    await InvokeEntrySpotPacketAsync(
                        runtime,
                        "route-target-node",
                        "api",
                        new SpotRouteRequestCallerCommand("request-via-api"));
                    return recorder.Replies.Contains("reply:request-via-api");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    [Fact]
    public async Task RegistryActorSessionBindings_Preserve_Reconnected_Binding_On_Stale_Unbind()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString(Guid.NewGuid().ToString("N")[..16]);

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });
            options.AddRouteMeshChannel("session", routed =>
            {
                routed.Bind(routeChannelEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = sessionRid);
            });
            options.UseRegistryActorSessionBindings("registry-session");
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();

        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var store = frameworkHost.Services.GetRequiredService<IZLinkActorSessionBindingStore>();
        var actorId = $"actor-{Guid.NewGuid():N}";
        var first = new ZLinkActorSessionBinding(actorId, sessionRid, "old-token");
        var second = new ZLinkActorSessionBinding(actorId, sessionRid, "new-token");

        await RetryAsync(
            async () =>
            {
                await store.BindSessionAsync(first, CancellationToken.None);
                return true;
            },
            static result => result,
            TimeSpan.FromSeconds(5));
        await store.BindSessionAsync(second, CancellationToken.None);
        await store.UnbindSessionAsync(
            new ZLinkActorSessionUnbind(actorId, first.BindingToken),
            CancellationToken.None);

        var route = await store.FindSessionAsync(actorId, CancellationToken.None);
        Assert.Equal(second.BindingToken, route.BindingToken);
        Assert.Equal(second.SessionRouterId, route.SessionRouterId);

        await store.UnbindSessionAsync(
            new ZLinkActorSessionUnbind(actorId, second.BindingToken),
            CancellationToken.None);
        await Assert.ThrowsAsync<ZLinkFrameworkException>(() =>
            store.FindSessionAsync(actorId, CancellationToken.None).AsTask());

        await frameworkHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Publishes_To_TargetChannel()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddSingleton<SpotLifecycleRecorder>();
        subscriberBuilder.Services.AddScoped<ExternalStageEventHandler>();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub();
                spot.AddSpotFactory<ExternalSubscriberStageSpot>("subscriber-stage");
            });
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("publisher-node", spot =>
            {
                spot.Bind(publisherNodeEndpoint);
                spot.EnablePubSub();
                spot.AttachSpotMeshPublisherClient("game.stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await registryHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var manager = subscriberHost.Services.GetRequiredService<IZLinkSpotManager>();
        _ = await manager.CreateAsync("subscriber-stage");

        var publisher = publisherHost.Services.GetRequiredService<IZLinkSpotPublisherClient>();
        var recorder = subscriberHost.Services.GetRequiredService<SpotLifecycleRecorder>();
        var publisherRuntime = publisherHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var subscriberRuntime = subscriberHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();

        await RetryAsync(
            async () => (await manager.ListAsync()).Count,
            count => count == 1,
            TimeSpan.FromSeconds(10));

        try
        {
            await RetryAsync(
                async () =>
	                {
	                    await publisher.Publish(
	                            "game.stage",
	                            "stage.external",
	                            new ExternalStageEvent("external"))
	                        .Submit();
	                    await Task.Yield();
	                    return recorder.ExternalEvents.Count;
	                },
                count => count > 0,
                TimeSpan.FromSeconds(10));
        }
        catch (Exception ex) when (ex is TimeoutException or ZlinkException)
        {
            var publisherSnapshot = publisherRuntime.GetSpotMonitoringSnapshot("publisher-node");
            var subscriberSnapshot = subscriberRuntime.GetSpotMonitoringSnapshot("subscriber-node");
            var publisherBundle = publisherRuntime.GetSpotPublisherBundle("game.stage");
            var directPublishResult = false;
            var rawReceived = "probe-unavailable";
            var probeParts = ZLinkEnvelopeCodec.EncodeParts(
                new ZLinkEnvelopeHeader(
                    ZLinkMessageKind.Publish,
                    "game.stage",
                    nameof(ExternalStageEvent),
                    ZLinkEnvelopeCodec.DefaultContentType,
                    null,
                    null,
                    "stage.external",
                    null,
                    null),
                new ExternalStageEvent("probe"),
                typeof(ExternalStageEvent));
            try
            {
                directPublishResult = publisherBundle.Spot.Publish(
                    "stage.external",
                    probeParts,
                    global::Systems.Zlink.SendFlags.None);
            }
            finally
            {
                ZLinkMessageParts.DisposeAll(probeParts);
            }

            var subscriberActivation = GetSingleSpotActivation(subscriberRuntime, "subscriber-node");
            var pumpState = GetSubscriptionPumpState(subscriberActivation);
            var subscriptionState =
                $"Messages={subscriberActivation.SubscriptionMessageCount},Dispatches={subscriberActivation.SubscriptionDispatchCount},Ignores={subscriberActivation.SubscriptionIgnoreCount},LastTopic={subscriberActivation.LastSubscriptionTopic},LastPacket={subscriberActivation.LastSubscriptionMessageName}";
            var publisherPeers = string.Join(';',
                publisherSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ChannelName}"));
            var subscriberPeers = string.Join(';',
                subscriberSnapshot.Peers.Select(static entry =>
                    $"{entry.Source}:{entry.PeerEndpoint}:{entry.State}:{entry.ChannelName}"));
            throw new TimeoutException(
                $"SPOT outbound publish failed. PublisherStatus={publisherSnapshot.Status}, PublisherPeers={publisherPeers}, PublisherSubjects={string.Join(';', publisherSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, SubscriberStatus={subscriberSnapshot.Status}, SubscriberPeers={subscriberPeers}, SubscriberSubjects={string.Join(';', subscriberSnapshot.Subjects.Select(static entry => $"{entry.Role}:{entry.Subject}:{entry.ReadyPeerCount}/{entry.ActivePeerCount}"))}, DirectPublishResult={directPublishResult}, RawReceived={rawReceived}, PumpState={pumpState}, SubscriptionState={subscriptionState}, ExternalEvents={recorder.ExternalEvents.Count}",
                ex);
        }

        Assert.Contains("external", recorder.ExternalEvents);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task OutboundOnly_SpotPublisherClient_Reaches_RawSubscriber_On_FrameworkNode()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var subscriberNodeEndpoint = GetFreeTcpEndpoint();
        var publisherNodeEndpoint = GetFreeTcpEndpoint();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var subscriberBuilder = Host.CreateApplicationBuilder();
        subscriberBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("subscriber-node", spot =>
            {
                spot.Bind(subscriberNodeEndpoint);
                spot.EnablePubSub();
            });
        });

        var publisherBuilder = Host.CreateApplicationBuilder();
        publisherBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", discovery =>
            {
                discovery.Add(registryRouterEndpoint);
            });

            options.AddSpotNode("publisher-node", spot =>
            {
                spot.Bind(publisherNodeEndpoint);
                spot.EnablePubSub();
                spot.AttachSpotMeshPublisherClient("game.stage");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var subscriberHost = subscriberBuilder.Build();
        using var publisherHost = publisherBuilder.Build();

        await registryHost.StartAsync();
        await subscriberHost.StartAsync();
        await publisherHost.StartAsync();

        var publisherRuntime = publisherHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var subscriberRuntime = subscriberHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var publisher = publisherHost.Services.GetRequiredService<IZLinkSpotPublisherClient>();
        var subscriberNodeRuntime = GetSpotNodeRuntime(subscriberRuntime, "subscriber-node");

        await using var rawSubscriber = subscriberNodeRuntime.Node.CreateSpot();
        rawSubscriber.SetSubscription("stage.external");

        using var received = new global::Systems.Zlink.TopicMessage();
        await RetryAsync(
            async () =>
            {
                await publisher.Publish(
                        "game.stage",
                        "stage.external",
                        new ExternalStageEvent("raw"))
                    .Submit();
                await Task.Yield();

                try
                {
                    return rawSubscriber.Subscribe(received,
                        global::Systems.Zlink.RecvFlags.DontWait);
                }
                catch (global::Systems.Zlink.ZlinkRecvException ex)
                    when (ex.Result == global::Systems.Zlink.ZlinkRecvException.ErrorCode.NoData)
                {
                    return false;
                }
            },
            static ok => ok,
            TimeSpan.FromSeconds(10));

        Assert.Equal("stage.external", received.Topic);

        await publisherHost.StopAsync();
        await subscriberHost.StopAsync();
        await registryHost.StopAsync();
    }

    [Fact]
    public async Task SpotActorJoin_Move_And_Submit_Run_Through_SpotExecutionContext()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorJoinHandler>();
        builder.Services.AddScoped<ActorJoinViaContextHandler>();
        builder.Services.AddScoped<ActorDispatchHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddActorFactory<TestActorFactory>("test");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();

        var first = await manager.CreateAsync("actor-stage");
        var second = await manager.CreateAsync("actor-stage");
        var actor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-1", "test")).Actor;

        var firstReply = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            first.SpotRid,
            actor,
            new JoinStageRequest("room-1"));
        Assert.Equal("room-1", firstReply.RoomId);
        Assert.Equal(first.SpotRid, actor.Spot?.Context.SpotRid);
        Assert.Equal(first.SpotRid, actor.Context.SpotRid);

        var secondReply = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            second.SpotRid,
            actor,
            new JoinStageRequest("room-2"));
        Assert.Equal("room-2", secondReply.RoomId);
        Assert.Equal(second.SpotRid, actor.Spot?.Context.SpotRid);
        Assert.Equal(second.SpotRid, actor.Context.SpotRid);
        await RetryAsync(
            () => recorder.SpotActorJoins.Contains($"actor-1@{second.SpotRid.ToHex()}")
                && recorder.SpotActorLeaves.Contains($"actor-1@{first.SpotRid.ToHex()}"),
            TimeSpan.FromSeconds(5));

        var contextActor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-context", "test")).Actor;
        await actorRuntime.AttachActorAsync(contextActor, new TestStream("session-context"));
        using (var joinBody = global::Systems.Zlink.Message.FromString(first.SpotRid.ToHex()))
        {
            await actorRuntime.SubmitActorAsync(
                contextActor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "join-via-context",
                    ZlinkStreamMetadata.Empty),
                joinBody);
        }

        Assert.Equal(first.SpotRid, contextActor.Spot?.Context.SpotRid);
        Assert.Equal("room-context", contextActor.CurrentRoomId);

        using (var contextDispatchBody = global::Systems.Zlink.Message.FromString("context-payload"))
        {
            await actorRuntime.SubmitActorAsync(
                contextActor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "dispatch-after-context-join",
                    ZlinkStreamMetadata.Empty),
                contextDispatchBody);
        }

        Assert.Contains("context-payload", recorder.DispatchBodies);
        Assert.Contains("room-context", recorder.DispatchRooms);
        Assert.Contains(first.SpotRid.ToHex(), recorder.DispatchSpotRids);

        var stream = new TestStream("session-1");
        await actorRuntime.AttachActorAsync(actor, stream);

        using var header = global::Systems.Zlink.Message.FromString("header");
        using var body = global::Systems.Zlink.Message.FromString("payload");
        await actorRuntime.SubmitActorAsync(
            actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                ZlinkStreamHeaderFlags.None,
                null,
                "dispatch",
                ZlinkStreamMetadata.Empty),
            body);

        await RetryAsync(
            () => recorder.DispatchBodies.Contains("payload"),
            TimeSpan.FromSeconds(5));

        Assert.False(recorder.ConcurrentViolation);
        Assert.Equal("room-2", recorder.DispatchRooms.LastOrDefault());
        Assert.DoesNotContain("room-1", recorder.DispatchRooms);

        await actorRuntime.DisconnectActorAsync(actor, stream);
        await actorRuntime.DisconnectActorAsync(
            contextActor,
            new TestStream("session-context"));

        await host.StopAsync();
    }

    [Fact]
    public async Task CreateLocalActorAsync_Coalesces_Concurrent_Creation_For_Same_ActorId()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddSingleton<ConcurrentActorFactoryRecorder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("actor.factory", _ => { });
            options.AddActorFactory<ConcurrentActorFactory>("test");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ConcurrentActorFactoryRecorder>();
        var calls = Enumerable.Range(0, 8)
            .Select(_ => runtime.CreateLocalActorAsync("actor-concurrent", "test").AsTask())
            .ToArray();

        await recorder.FirstFactoryCall.Task.WaitAsync(TimeSpan.FromSeconds(5));
        await Task.Delay(100);
        Assert.Equal(1, Volatile.Read(ref recorder.CreateCount));

        recorder.ReleaseFactory.SetResult();

        var results = await Task.WhenAll(calls).WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Equal(1, Volatile.Read(ref recorder.CreateCount));
        Assert.Equal(1, results.Count(static result => result.Created));
        Assert.All(results, result => Assert.Same(results[0].Actor, result.Actor));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_And_UserSpot_ActorPacketRegistries_Dispatch_ActorPackets()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<RegistryEntryJoinHandler>();
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddScoped<RegistryEntryLeftHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.registry", _ => { });
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("registry-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
                spot.AddSpotFactory<RegistryStageSpot>("registry-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var first = await manager.CreateAsync("registry-stage");
        var second = await manager.CreateAsync("registry-stage");
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("registry-actor", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actor, new TestStream("registry-session"));

        using (var joinBody = global::Systems.Zlink.Message.FromString(first.SpotRid.ToHex()))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "entry-join",
                    ZlinkStreamMetadata.Empty),
                joinBody);
        }

        Assert.Equal("entry-room", actor.CurrentRoomId);
        Assert.Contains($"entry:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains($"entry-left:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains($"joined:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);

        using (var dispatchBody = global::Systems.Zlink.Message.FromString("payload"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "spot-dispatch",
                    ZlinkStreamMetadata.Empty),
                dispatchBody);
        }

        Assert.Contains($"dispatch:registry-actor:entry-room:payload:{first.SpotRid.ToHex()}", recorder.Events);

        _ = await actorRuntime.JoinActorAsync<RegistryJoinRequest, RegistryJoinReply>(
            second.SpotRid,
            actor,
            new RegistryJoinRequest("second-room"));

        Assert.Contains($"left:registry-actor:{first.SpotRid.ToHex()}", recorder.Events);
        Assert.Contains($"joined:registry-actor:{second.SpotRid.ToHex()}", recorder.Events);

        var currentSpot = actor.Spot ?? throw new InvalidOperationException("Actor is not joined.");
        await currentSpot.Context.LeaveActorAsync(actor);
        actor.DetachSpot(currentSpot);

        Assert.Contains($"entry-joined:registry-actor:{second.SpotRid.ToHex()}", recorder.Events);

        await actorRuntime.DisconnectActorAsync(
            actor,
            new TestStream("registry-session"));

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorDispatch_Rechecks_CurrentLocation_After_Waiting_For_ActorMailbox()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<RegistryStageSpot>();
        builder.Services.AddScoped<EntrySpotJoinBlockingHandler>();
        builder.Services.AddScoped<RegistryStageJoinHandler>();
        builder.Services.AddScoped<RegistryStageDispatchHandler>();
        builder.Services.AddScoped<RegistryStageJoinedHandler>();
        builder.Services.AddScoped<RegistryStageLeftHandler>();
        builder.Services.AddScoped<RegistryEntryJoinedHandler>();
        builder.Services.AddScoped<RegistryEntryLeftHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.registry-location", _ => { });
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("registry-location-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
                spot.AddSpotFactory<RegistryStageSpot>("registry-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var stage = await manager.CreateAsync("registry-stage");
        var actor = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("registry-location-actor", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actor, new TestStream("registry-location-session"));

        Task? joinTask = null;
        Task? dispatchTask = null;
        try
        {
            joinTask = SubmitEntrySpotStringAsync(
                actorRuntime,
                actor,
                "entry-join-block",
                stage.SpotRid.ToHex());
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            dispatchTask = SubmitEntrySpotStringAsync(
                actorRuntime,
                actor,
                "spot-dispatch",
                "after-join");
            Assert.False(dispatchTask.IsCompleted, "dispatch should wait for the same actor mailbox.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await joinTask!.WaitAsync(TimeSpan.FromSeconds(5));
        await dispatchTask!.WaitAsync(TimeSpan.FromSeconds(5));

        Assert.Contains(
            $"dispatch:registry-location-actor:entry-room:after-join:{stage.SpotRid.ToHex()}",
            recorder.Events);

        await actorRuntime.DisconnectActorAsync(
            actor,
            new TestStream("registry-location-session"));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_ActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<EntrySpotBlockingHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-mailbox", _ => { });
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("entry-mailbox-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-actor-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-actor-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        try
        {
            actorABlocked = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "entry-block",
                "block-a");
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            actorASecond = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "entry-record",
                "after-a");
            var actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "entry-record",
                "record-b");

            await actorBPacket.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains("record:entry-actor-b:record-b", mailboxRecorder.Events);
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("record:entry-actor-a:after-a", mailboxRecorder.Events);

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("entry-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("entry-session-b"));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_PacketHandlers_Are_Dispatched_Without_EntrySpot_Serialization()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotCallbackRecorder>();
        builder.Services.AddScoped<GeneralEntrySpot>();
        builder.Services.AddScoped<EntrySpotGeneralBlockingHandler>();
        builder.Services.AddScoped<EntrySpotGeneralRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-general", _ => { });
            options.AddSpotNode("entry-general-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<GeneralEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<EntrySpotCallbackRecorder>();
        var nodeRuntime = GetSpotNodeRuntime(runtime, "entry-general-node");
        var activation = nodeRuntime.EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var blocking = new EntrySpotGeneralBlockingCommand("block");
        var blockingHeader = CreateEntrySpotEnvelopeHeader("game.entry-general", blocking);
        Assert.True(activation.TryResolvePacket(blockingHeader, out var blockingDescriptor));
        var blockingDispatch = activation
            .InvokePacketAsync(blockingDescriptor!, blocking, CancellationToken.None)
            .AsTask();
        await recorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

        var record = new EntrySpotGeneralRecordCommand("record");
        var recordHeader = CreateEntrySpotEnvelopeHeader("game.entry-general", record);
        Assert.True(activation.TryResolvePacket(recordHeader, out var recordDescriptor));
        var recordDispatch = activation
            .InvokePacketAsync(recordDescriptor!, record, CancellationToken.None)
            .AsTask();

        await RetryAsync(
            () => recorder.Events.Contains($"record:record:{activation.SpotRid.ToHex()}"),
            TimeSpan.FromSeconds(5));
        Assert.DoesNotContain("block-end:block", recorder.Events);

        recorder.ReleaseBlocking.TrySetResult();
        await Task.WhenAll(blockingDispatch, recordDispatch).WaitAsync(TimeSpan.FromSeconds(5));
        await RetryAsync(
            () => recorder.Events.Contains("block-end:block"),
            TimeSpan.FromSeconds(5));

        await host.StopAsync();
    }

    [Fact]
    public async Task EntrySpot_NativeActorReadableBatch_Dispatches_Actors_In_Parallel()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<RegistryEntrySpot>();
        builder.Services.AddScoped<EntrySpotBlockingHandler>();
        builder.Services.AddScoped<EntrySpotRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.entry-native-batch", _ => { });
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("entry-native-batch-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddEntrySpot<RegistryEntrySpot>();
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var activation = actorRuntime
            .GetSpotNodeRuntime("entry-native-batch-node")
            .EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-native-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("entry-native-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("entry-native-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("entry-native-session-b"));

        var dispatch = ZLinkEntrySpotActorDispatcher.DispatchAsync(
            actorRuntime,
            activation,
            [
                CreateEntryActorHeaderPart(actorA, "entry-block"),
                CreateEntryActorBodyPart(actorA, "block-a"),
                CreateEntryActorHeaderPart(actorB, "entry-record"),
                CreateEntryActorBodyPart(actorB, "record-b"),
                CreateEntryActorHeaderPart(actorA, "entry-record"),
                CreateEntryActorBodyPart(actorA, "after-a"),
            ],
            CancellationToken.None);

        try
        {
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));
            await RetryAsync(
                () => mailboxRecorder.Events.Contains("record:entry-native-b:record-b"),
                TimeSpan.FromSeconds(5));
            Assert.DoesNotContain("record:entry-native-a:after-a", mailboxRecorder.Events);
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await dispatch.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("record:entry-native-a:after-a", mailboxRecorder.Events);

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("entry-native-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("entry-native-session-b"));

        await host.StopAsync();
    }

    [Fact]
    public async Task LocalActorPackets_Are_Serialized_Per_Actor_And_Parallel_Across_Actors()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<EntrySpotActorRegistryRecorder>();
        builder.Services.AddSingleton<EntrySpotMailboxRecorder>();
        builder.Services.AddScoped<LocalActorBlockingHandler>();
        builder.Services.AddScoped<LocalActorRecordingHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddActorFactory<RegistryTestActorFactory>("registry");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var registryRecorder = host.Services.GetRequiredService<EntrySpotActorRegistryRecorder>();
        var mailboxRecorder = host.Services.GetRequiredService<EntrySpotMailboxRecorder>();
        var actorA = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("local-actor-a", "registry")).Actor;
        var actorB = (RegistryTestActor)(await actorRuntime.CreateLocalActorAsync("local-actor-b", "registry")).Actor;
        await actorRuntime.AttachActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.AttachActorAsync(actorB, new TestStream("local-session-b"));

        Task? actorABlocked = null;
        Task? actorASecond = null;
        try
        {
            actorABlocked = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "local-block",
                "block-a");
            await mailboxRecorder.BlockingStarted.Task.WaitAsync(TimeSpan.FromSeconds(5));

            actorASecond = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorA,
                "local-record",
                "after-a");
            var actorBPacket = SubmitEntrySpotStringAsync(
                actorRuntime,
                actorB,
                "local-record",
                "record-b");

            await actorBPacket.WaitAsync(TimeSpan.FromSeconds(5));
            Assert.Contains("local-record:local-actor-b:record-b", mailboxRecorder.Events);
            Assert.False(actorASecond.IsCompleted, "same actor packet ran before the blocking packet completed.");
        }
        finally
        {
            mailboxRecorder.ReleaseBlocking.TrySetResult();
        }

        await actorABlocked!.WaitAsync(TimeSpan.FromSeconds(5));
        await actorASecond!.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.Contains("local-record:local-actor-a:after-a", mailboxRecorder.Events);

        await actorRuntime.DisconnectActorAsync(actorA, new TestStream("local-session-a"));
        await actorRuntime.DisconnectActorAsync(actorB, new TestStream("local-session-b"));

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorContext_RequestChannel_Uses_Global_Client_Before_Join_And_Spot_Client_After_Join()
    {
        var preJoinApi = GetFreeTcpEndpoint();
        var postJoinApi = GetFreeTcpEndpoint();
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorJoinHandler>();
        builder.Services.AddScoped<ActorPreJoinChannelHandler>();
        builder.Services.AddScoped<ActorPostJoinChannelHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<SpotIntegrationTests>();
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddActorFactory<TestActorFactory>("test");
            options.AddClientServerChannel("actor-pre-api", channel =>
            {
                channel.EnableServer(server => server.Bind(preJoinApi));
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(connections => connections.Connect(preJoinApi));
                });
            });
            options.AddClientServerChannel("actor-post-api", channel =>
            {
                channel.EnableServer(server => server.Bind(postJoinApi));
            });
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AttachClientServerChannelClient("actor-post-api", client =>
                {
                    client.UseManualConnections(connections => connections.Connect(postJoinApi));
                });
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var created = await manager.CreateAsync("actor-stage");
        var actor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-context-client", "test")).Actor;
        await actorRuntime.AttachActorAsync(actor, new TestStream("session-context-client"));

        using (var preJoinBody = global::Systems.Zlink.Message.FromString("before"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "request-channel-before-join",
                    ZlinkStreamMetadata.Empty),
                preJoinBody);
        }

        Assert.Contains("before:before", recorder.ChannelReplies);

        _ = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            created.SpotRid,
            actor,
            new JoinStageRequest("room-context-client"));

        using (var postJoinBody = global::Systems.Zlink.Message.FromString("after"))
        {
            await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "request-channel-after-join",
                    ZlinkStreamMetadata.Empty),
                postJoinBody);
        }

        Assert.Contains("after:after", recorder.ChannelReplies);

        await actorRuntime.DisconnectActorAsync(
            actor,
            new TestStream("session-context-client"));

        await host.StopAsync();
    }

    [Fact]
    public async Task ActorSessionState_Filters_StaleDisconnect_And_Only_Disconnects_CurrentStream()
    {
        var spotNode = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<ActorIntegrationRecorder>();
        builder.Services.AddScoped<ActorJoinHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.UseSpotDiscovery("game.stage", _ => { });
            options.AddActorFactory<TestActorFactory>("test");
            options.AddSpotNode("actor-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddSpotFactory<ActorStageSpot>("actor-stage");
            });
        });

        using var host = builder.Build();
        await host.StartAsync();

        var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
        var actorRuntime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var recorder = host.Services.GetRequiredService<ActorIntegrationRecorder>();
        var created = await manager.CreateAsync("actor-stage");
        var actor = (TestActor)(await actorRuntime.CreateLocalActorAsync("actor-2", "test")).Actor;

        _ = await actorRuntime.JoinActorAsync<JoinStageRequest, JoinStageReply>(
            created.SpotRid,
            actor,
            new JoinStageRequest("room-disconnect"));

        var staleStream = new TestStream("session-stale");
        var currentStream = new TestStream("session-current");
        await actorRuntime.AttachActorAsync(actor, staleStream);
        await actorRuntime.AttachActorAsync(actor, currentStream);

        await actorRuntime.DisconnectActorAsync(actor, staleStream);
        Assert.Equal(0, recorder.DisconnectCount);
        await actorRuntime.DisconnectActorAsync(actor, currentStream);
        Assert.Equal(1, recorder.DisconnectCount);

        await host.StopAsync();
    }

    private static async Task<IHost> CreateHostAsync(
        string ordersServer,
        string spotNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotEventsRecorder>();
        builder.Services.AddSingleton<OrdersRecorder>();
        builder.Services.AddScoped<SpotScopeMarker>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<SpotIntegrationTests>();
            options.UseSpotDiscovery("game.stage", _ => { });

            options.AddClientServerChannel("orders", channel =>
            {
                channel.EnableServer(server => server.Bind(ordersServer));
            });

            options.AddSpotNode("stage-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AttachClientServerChannelClient("orders", client =>
                {
                    client.UseManualConnections(connections => connections.Connect(ordersServer));
                });
                spot.AddSpotFactory<StageSpot>("stage");
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static async Task<IHost> CreatePayloadHostAsync(string spotNode)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotCreatePayloadRecorder>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddSpotNode("payload-node", spot =>
            {
                spot.Bind(spotNode);
                spot.AddSpotFactory<CreatePayloadStageSpot>("payload-stage");
                spot.AddSpotFactory<CreatePayloadStageSpot>("other-payload-stage");
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static async Task<IHost> CreateSpotRouteTransportHostAsync(
        SpotRouteTransportKind transportKind,
        string routerChannelId,
        string channelEndpoint,
        string spotNodeEndpoint)
    {
        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotRouteTransportRecorder>();
        builder.Services.AddSingleton<FixedSpotRouteResolver>();
        builder.Services.AddSingleton<IZLinkSpotRouteResolver>(
            services => services.GetRequiredService<FixedSpotRouteResolver>());
        builder.Services.AddScoped<SpotRouteTargetCommandHandler>();
        builder.Services.AddScoped<SpotRouteTargetRequestHandler>();
        builder.Services.AddScoped<SpotRouteSendCallerHandler>();
        builder.Services.AddScoped<SpotRouteRequestCallerHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            AddRouterChannel(options, transportKind, routerChannelId, channelEndpoint);
            options.UseSpotDiscovery("spot.route.transport", _ => { });
            options.AddSpotNode("route-target-node", spot =>
            {
                spot.Bind(spotNodeEndpoint);
                spot.EnableRouter(router =>
                {
                    router.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromBytes(
                            Encoding.UTF8.GetBytes("target-node"));
                    });
                });
                spot.AcceptSpotRoutesFromChannel(
                    routerChannelId,
                    routes => routes.UseManualConnections(
                        peers => peers.Connect(channelEndpoint)));
                spot.AddEntrySpot<SpotRouteCallerEntrySpot>();
                spot.AddSpotFactory<SpotRouteTargetSpot>("route-target");
            });
        });

        var host = builder.Build();
        await host.StartAsync();
        return host;
    }

    private static void AddRouterChannel(
        IZLinkFrameworkOptions options,
        SpotRouteTransportKind transportKind,
        string routerChannelId,
        string channelEndpoint)
    {
        switch (transportKind)
        {
            case SpotRouteTransportKind.ClientServer:
                options.AddClientServerChannel(routerChannelId, channel =>
                {
                    channel.EnableServer(server =>
                    {
                        server.Bind(channelEndpoint);
                        server.ConfigureRouting(routing =>
                        {
                            routing.RoutingId = RoutingId.FromString("aabbcc10");
                        });
                    });
                });
                break;

            case SpotRouteTransportKind.RouteMesh:
                options.AddRouteMeshChannel(routerChannelId, route =>
                {
                    route.Bind(channelEndpoint);
                    route.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromString("aabbcc11");
                    });
                });
                break;

            default:
                throw new ArgumentOutOfRangeException(nameof(transportKind), transportKind, null);
        }
    }

    private static async Task InvokeEntrySpotPacketAsync<TMessage>(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName,
        string channelName,
        TMessage message)
    {
        var activation = runtime.GetSpotNodeRuntime(spotNodeName).EntrySpotActivation
            ?? throw new InvalidOperationException("Entry Spot activation was not created.");
        var header = CreateEntrySpotEnvelopeHeader(channelName, message);
        Assert.True(activation.TryResolvePacket(header, out var descriptor));
        await activation.InvokePacketAsync(descriptor!, message, CancellationToken.None)
            .ConfigureAwait(false);
    }

    private static async Task VerifyAcceptedRouteChannelSendToSpotAsync(
        SpotRouteTransportKind transportKind,
        string routerChannelId)
    {
        var channelEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var host = await CreateSpotRouteTransportHostAsync(
            transportKind,
            routerChannelId,
            channelEndpoint,
            spotNodeEndpoint);
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            var target = await manager.GetOrCreateAsync(
                "route-target",
                RoutingId.FromBytes(Encoding.UTF8.GetBytes($"spot{routerChannelId}")));
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, routerChannelId);

            await RetryAsync(
                async () =>
                {
                    var header = ZLinkClientCallCodec.CreateEnvelope(
                        ZLinkMessageKind.Command,
                        routerChannelId,
                        ZLinkMessageNameResolver.ResolveFromType(typeof(SpotRouteTargetCommand))
                            ?? throw new InvalidOperationException("Message name is required."));
                    var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                        header,
                        new SpotRouteTargetCommand($"direct:{routerChannelId}"));
                    await runtime.SendSpotViaRouterChannelAsync(
                            routerChannelId,
                            nodeRuntime.Node.RoutingId,
                            target.SpotRid,
                            parts,
                            CancellationToken.None)
                        .ConfigureAwait(false);
                    return recorder.Commands.Contains($"direct:{routerChannelId}");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await StopAndDisposeHostAsync(host);
        }
    }

    private static async Task RetryAsync(Func<bool> predicate, TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            if (predicate())
            {
                return;
            }

            await Task.Delay(PollingInterval);
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private static async Task<T> RetryAsync<T>(
        Func<Task<T>> action,
        Func<T, bool> predicate,
        TimeSpan timeout)
    {
        var deadline = DateTime.UtcNow + timeout;
        Exception? lastError = null;

        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var result = await action();
                if (predicate(result))
                {
                    return result;
                }
            }
            catch (Exception ex)
            {
                lastError = ex;
            }

            await Task.Delay(PollingInterval);
        }

        if (lastError is not null)
        {
            throw lastError;
        }

        throw new TimeoutException("SPOT integration retry timed out.");
    }

    private static string GetFreeTcpEndpoint()
    {
        return ChannelMessagingTestSupport.GetTcpEndpoint();
    }

    private static async Task WaitForAcceptedRoutePeerAsync(
        ZLinkSpotNodeRuntime nodeRuntime,
        string channelName,
        bool requireConnected = false)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        IReadOnlyList<ZLinkSpotNodePeerEntry> peers = [];
        while (DateTime.UtcNow < deadline)
        {
            peers = nodeRuntime.Node.PeersSnapshot();
            if (peers.Any(peer => peer.ChannelName == channelName
                    && (!requireConnected || peer.State == ZLinkSpotPeerState.Connected)))
            {
                await Task.Delay(TimeSpan.FromMilliseconds(300));
                return;
            }

            await Task.Delay(PollingInterval);
        }

        var peerSummary = string.Join(
            ", ",
            peers.Select(peer => $"{peer.ChannelName}:{peer.PeerEndpoint}:{peer.State}"));
        throw new TimeoutException(
            $"SPOT route peer '{channelName}' did not connect. Peers: {peerSummary}");
    }

    private static async Task StopAndDisposeHostAsync(IHost host)
    {
        try
        {
            await host.StopAsync();
        }
        finally
        {
            host.Dispose();
        }
    }

    public sealed class StageSpot : IZLinkSpot
    {
        private readonly SpotScopeMarker _scopeMarker;
        private readonly SpotEventsRecorder _events;
        private readonly IZLinkSpotClient _spotClient;

        public StageSpot(
            IZLinkSpotContext context,
            SpotScopeMarker scopeMarker,
            SpotEventsRecorder events,
            IZLinkSpotClient spotClient)
        {
            Context = context;
            _scopeMarker = scopeMarker;
            _events = events;
            _spotClient = spotClient;
        }

        public IZLinkSpotContext Context { get; }

        public string ScopeId => _scopeMarker.Id;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _events.RecordInitialized(Context.SpotRid, _scopeMarker.Id);

            await _spotClient.SendChannel("orders", new StageBootCommand(_scopeMarker.Id)).Submit(cancellationToken);
        }

        public ValueTask OnClosingAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            _events.RecordClosing(Context.SpotRid);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class CreatePayloadStageSpot(
        IZLinkSpotContext context,
        SpotCreatePayloadRecorder recorder) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask OnCreateAsync(
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken)
        {
            await recorder.RecordAsync(createParts, cancellationToken);
        }
    }

    public sealed class ActorStageSpot : IZLinkSpot
    {
        private readonly ActorIntegrationRecorder _recorder;
        private int _inFlight;

        public ActorStageSpot(IZLinkSpotContext context, ActorIntegrationRecorder recorder)
        {
            Context = context;
            _recorder = recorder;
        }

        public IZLinkSpotContext Context { get; }

        public void Configure()
        {
            Context.AddActorJoin<ActorJoinHandler, TestActor, JoinStageRequest, JoinStageReply>();
        }

        internal IDisposable EnterScope(string source)
        {
            if (Interlocked.Increment(ref _inFlight) != 1)
            {
                _recorder.ConcurrentViolation = true;
                _recorder.ScopeViolations.Enqueue(source);
            }

            return new ScopeLease(this);
        }

        internal async ValueTask<JoinStageReply> JoinActorAsync(
            TestActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("join");

            if (actor.Spot is ActorStageSpot current && !ReferenceEquals(current, this))
            {
                await current.Context.LeaveActorAsync(actor, cancellationToken);
                actor.DetachSpot(current);
            }

            await Context.JoinActorAsync(actor, cancellationToken);
            actor.AttachSpot(this);
            actor.CurrentRoomId = request.RoomId;

            return new JoinStageReply(request.RoomId);
        }

        internal async ValueTask LeaveActorAsync(
            TestActor actor,
            CancellationToken cancellationToken)
        {
            using var _ = EnterScope("leave");

            await Context.LeaveActorAsync(actor, cancellationToken);
            actor.DetachSpot(this);
            actor.CurrentRoomId = null;
        }

        public ValueTask OnActorJoinedAsync(
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (info.CurrentActorId is { } actorId
                && info.CurrentSpotRid is { } spotRid)
            {
                _recorder.SpotActorJoins.Enqueue($"{actorId}@{spotRid.ToHex()}");
            }

            return ValueTask.CompletedTask;
        }

        public ValueTask OnActorLeftAsync(
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (info.PreviousActorId is { } actorId
                && info.PreviousSpotRid is { } spotRid)
            {
                _recorder.SpotActorLeaves.Enqueue($"{actorId}@{spotRid.ToHex()}");
            }

            return ValueTask.CompletedTask;
        }

        private sealed class ScopeLease(ActorStageSpot spot) : IDisposable
        {
            public void Dispose()
            {
                Interlocked.Decrement(ref spot._inFlight);
            }
        }
    }

    public sealed record JoinStageRequest(string RoomId);

    public sealed record JoinStageReply(string RoomId);

    public sealed class ActorJoinHandler : IZLinkSpotActorJoinHandler<ActorStageSpot, TestActor, JoinStageRequest, JoinStageReply>
    {
        public ValueTask<JoinStageReply> HandleAsync(
            ActorStageSpot spot,
            TestActor actor,
            JoinStageRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinActorAsync(actor, request, cancellationToken);
        }
    }

    public sealed class TestActor(
        string actorId,
        IZLinkActorContext context,
        ActorIntegrationRecorder recorder) : IZLinkActor
    {

        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public ActorIntegrationRecorder Recorder { get; } = recorder;

        public ActorStageSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public void Configure()
        {
            Context.AddPacket<ActorPreJoinChannelHandler>("request-channel-before-join");
            Context.AddPacket<ActorPostJoinChannelHandler>("request-channel-after-join");
            Context.AddPacket<ActorJoinViaContextHandler>("join-via-context");
            Context.AddPacket<ActorDispatchHandler>("dispatch");
            Context.AddPacket<ActorDispatchHandler>("dispatch-after-context-join");
        }

        public void AttachSpot(ActorStageSpot spot)
        {
            Spot = spot;
        }

        public void DetachSpot(ActorStageSpot spot)
        {
            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            Interlocked.Increment(ref Recorder.DisconnectCount);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ActorPreJoinChannelHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.RequestChannel(
                    "actor-pre-api",
                    new ActorContextChannelReq(message))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<ActorContextChannelRes>(cancellationToken);

            actor.Recorder.ChannelReplies.Enqueue($"before:{reply.Value}");
        }
    }

    public sealed class ActorPostJoinChannelHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.RequestChannel(
                    "actor-post-api",
                    new ActorContextChannelReq(message))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<ActorContextChannelRes>(cancellationToken);

            actor.Recorder.ChannelReplies.Enqueue($"after:{reply.Value}");
        }
    }

    public sealed class ActorJoinViaContextHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public async ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(message),
                    new JoinStageRequest("room-context"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<JoinStageReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
        }
    }

    public sealed class ActorDispatchHandler
        : IZLinkActorPacketHandler<TestActor, string>
    {
        public ValueTask HandleAsync(
            TestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            var stageSpot = actor.Context.GetSpot<ActorStageSpot>();

            if (!ReferenceEquals(actor.Spot, stageSpot))
            {
                throw new InvalidOperationException("Actor context SPOT does not match actor SPOT.");
            }

            using var scope = stageSpot.EnterScope("dispatch");
            actor.Recorder.DispatchBodies.Enqueue(message);
            actor.Recorder.DispatchRooms.Enqueue(actor.CurrentRoomId ?? string.Empty);
            actor.Recorder.DispatchSpotRids.Enqueue(stageSpot.Context.SpotRid.ToHex());
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddActorPacket<RegistryEntryJoinHandler, RegistryTestActor>("entry-join");
            Context.AddActorPacket<EntrySpotJoinBlockingHandler, RegistryTestActor>("entry-join-block");
            Context.AddActorPacket<EntrySpotBlockingHandler, RegistryTestActor>("entry-block");
            Context.AddActorPacket<EntrySpotRecordingHandler, RegistryTestActor>("entry-record");
            Context.AddActorJoined<RegistryEntryJoinedHandler, RegistryTestActor>();
            Context.AddActorLeft<RegistryEntryLeftHandler, RegistryTestActor>();
        }
    }

    public sealed class GeneralEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddPacket<EntrySpotGeneralBlockingHandler>();
            Context.AddPacket<EntrySpotGeneralRecordingHandler>();
        }
    }

    public sealed class EntryTimerSpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddPacket<EntryTimerRecordingHandler>();
        }

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _ = await Context.AddTimer<EntryTimerBlockingHandler>(
                "entry-blocking",
                TimeSpan.FromMilliseconds(10),
                cancellationToken: cancellationToken);
        }
    }

    public sealed record EntryTimerRecordCommand(string Value);

    public sealed class EntryTimerBlockingHandler(EntrySpotTimerCallbackRecorder recorder)
        : IZLinkSpotTimerHandler<EntryTimerSpot>
    {
        public async ValueTask HandleAsync(
            EntryTimerSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = tick;
            recorder.Events.Enqueue("timer-start");
            recorder.TimerStarted.TrySetResult();
            await recorder.ReleaseTimer.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue("timer-end");
        }
    }

    public sealed class EntryTimerRecordingHandler(EntrySpotTimerCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<EntryTimerSpot, EntryTimerRecordCommand>
    {
        public ValueTask HandleAsync(
            EntryTimerSpot spot,
            EntryTimerRecordCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{message.Value}");
            recorder.Recorded.TrySetResult();
            return ValueTask.CompletedTask;
        }
    }

    public sealed class EntrySpotTimerCallbackRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource TimerStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseTimer { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource Recorded { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed record EntrySpotGeneralBlockingCommand(string Value);

    public sealed record EntrySpotGeneralRecordCommand(string Value);

    public sealed class EntrySpotGeneralBlockingHandler(EntrySpotCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<GeneralEntrySpot, EntrySpotGeneralBlockingCommand>
    {
        public async ValueTask HandleAsync(
            GeneralEntrySpot spot,
            EntrySpotGeneralBlockingCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            recorder.Events.Enqueue($"block-start:{message.Value}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"block-end:{message.Value}");
        }
    }

    public sealed class EntrySpotGeneralRecordingHandler(EntrySpotCallbackRecorder recorder)
        : IZLinkSpotPacketHandler<GeneralEntrySpot, EntrySpotGeneralRecordCommand>
    {
        public ValueTask HandleAsync(
            GeneralEntrySpot spot,
            EntrySpotGeneralRecordCommand message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{message.Value}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class EntrySpotCallbackRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource BlockingStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseBlocking { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed class RegistryStageSpot(IZLinkSpotContext context)
        : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddActorJoin<RegistryStageJoinHandler, RegistryTestActor, RegistryJoinRequest, RegistryJoinReply>();
            Context.AddActorPacket<RegistryStageDispatchHandler, RegistryTestActor>("spot-dispatch");
            Context.AddActorJoined<RegistryStageJoinedHandler, RegistryTestActor>();
            Context.AddActorLeft<RegistryStageLeftHandler, RegistryTestActor>();
        }

        public async ValueTask<RegistryJoinReply> JoinAsync(
            RegistryTestActor actor,
            RegistryJoinRequest request,
            CancellationToken cancellationToken)
        {
            if (actor.Spot is RegistryStageSpot current && !ReferenceEquals(current, this))
            {
                await current.Context.LeaveActorAsync(actor, cancellationToken);
                actor.DetachSpot(current);
            }

            await Context.JoinActorAsync(actor, cancellationToken);
            actor.AttachSpot(this);
            actor.CurrentRoomId = request.RoomId;
            return new RegistryJoinReply(request.RoomId);
        }
    }

    public sealed class RegistryEntryJoinHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string spotRid,
            CancellationToken cancellationToken)
        {
            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(spotRid),
                    new RegistryJoinRequest("entry-room"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<RegistryJoinReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
            recorder.Events.Enqueue($"entry:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            recorder.Events.Enqueue($"block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class EntrySpotJoinBlockingHandler(
        EntrySpotMailboxRecorder mailboxRecorder,
        EntrySpotActorRegistryRecorder registryRecorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string spotRid,
            CancellationToken cancellationToken)
        {
            mailboxRecorder.BlockingStarted.TrySetResult();
            await mailboxRecorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

            var reply = await actor.Context.JoinSpot(
                    global::Systems.Zlink.RoutingId.FromString(spotRid),
                    new RegistryJoinRequest("entry-room"))
                .Timeout(TimeSpan.FromSeconds(5))
                .SubmitAsync<RegistryJoinReply>(cancellationToken);

            actor.CurrentRoomId = reply.RoomId;
            registryRecorder.Events.Enqueue($"entry-block-joined:{actor.ActorId}:{spotRid}");
        }
    }

    public sealed class EntrySpotRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkEntrySpotActorSendHandler<RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class LocalActorBlockingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkActorPacketHandler<RegistryTestActor, string>
    {
        public async ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            recorder.Events.Enqueue($"local-block-start:{actor.ActorId}:{message}");
            recorder.BlockingStarted.TrySetResult();
            await recorder.ReleaseBlocking.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
            recorder.Events.Enqueue($"local-block-end:{actor.ActorId}:{message}");
        }
    }

    public sealed class LocalActorRecordingHandler(EntrySpotMailboxRecorder recorder)
        : IZLinkActorPacketHandler<RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"local-record:{actor.ActorId}:{message}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageJoinHandler
        : IZLinkSpotActorJoinHandler<RegistryStageSpot, RegistryTestActor, RegistryJoinRequest, RegistryJoinReply>
    {
        public ValueTask<RegistryJoinReply> HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            RegistryJoinRequest request,
            CancellationToken cancellationToken)
        {
            return spot.JoinAsync(actor, request, cancellationToken);
        }
    }

    public sealed class RegistryStageDispatchHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorSendHandler<RegistryStageSpot, RegistryTestActor, string>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            string message,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue(
                $"dispatch:{actor.ActorId}:{actor.CurrentRoomId}:{message}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageJoinedHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorJoinedHandler<RegistryStageSpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"joined:{actor.ActorId}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryStageLeftHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkSpotActorLeftHandler<RegistryStageSpot, RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryStageSpot spot,
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = info;
            _ = cancellationToken;
            recorder.Events.Enqueue($"left:{actor.ActorId}:{spot.Context.SpotRid.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryJoinedHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorJoinedHandler<RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-joined:{actor.ActorId}:{info.PreviousSpotRid?.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryEntryLeftHandler(EntrySpotActorRegistryRecorder recorder)
        : IZLinkEntrySpotActorLeftHandler<RegistryTestActor>
    {
        public ValueTask HandleAsync(
            RegistryTestActor actor,
            ZLinkSpotActorLifecycleInfo info,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            recorder.Events.Enqueue($"entry-left:{actor.ActorId}:{info.CurrentSpotRid?.ToHex()}");
            return ValueTask.CompletedTask;
        }
    }

    public sealed record RegistryJoinRequest(string RoomId);

    public sealed record RegistryJoinReply(string RoomId);

    public sealed class RegistryTestActor(
        string actorId,
        IZLinkActorContext context,
        EntrySpotActorRegistryRecorder recorder) : IZLinkActor
    {
        public string ActorId { get; } = actorId;

        public IZLinkActorContext Context { get; } = context;

        public EntrySpotActorRegistryRecorder Recorder { get; } = recorder;

        public RegistryStageSpot? Spot { get; private set; }

        public string? CurrentRoomId { get; set; }

        public void Configure()
        {
            Context.AddPacket<LocalActorBlockingHandler>("local-block");
            Context.AddPacket<LocalActorRecordingHandler>("local-record");
        }

        public void AttachSpot(RegistryStageSpot spot)
        {
            Spot = spot;
        }

        public void DetachSpot(RegistryStageSpot spot)
        {
            if (ReferenceEquals(Spot, spot))
            {
                Spot = null;
            }
        }

        public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class RegistryTestActorFactory(
        EntrySpotActorRegistryRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(
                new RegistryTestActor(actorId, context, recorder));
        }
    }

    public sealed class EntrySpotActorRegistryRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();
    }

    public sealed class EntrySpotMailboxRecorder
    {
        public ConcurrentQueue<string> Events { get; } = new();

        public TaskCompletionSource BlockingStarted { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseBlocking { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed class TestStream(string sessionId) : IZLinkStream
    {
        public string SessionId { get; } = sessionId;

        public global::Systems.Zlink.RoutingId? RoutingId => null;

        public string? LocalAddr => "local";

        public string? RemoteAddr => "remote";

        public bool Write(global::Systems.Zlink.Message payload, global::Systems.Zlink.SendFlags flags = global::Systems.Zlink.SendFlags.None)
        {
            _ = payload;
            _ = flags;
            return true;
        }

        public ValueTask CloseAsync(CancellationToken cancellationToken = default)
        {
            _ = cancellationToken;
            return ValueTask.CompletedTask;
        }
    }

    public sealed class TestActorFactory(ActorIntegrationRecorder recorder) : IZLinkActorFactory
    {
        public ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult<IZLinkActor>(new TestActor(actorId, context, recorder));
        }
    }

    public sealed class ConcurrentActorFactory(
        ConcurrentActorFactoryRecorder factoryRecorder,
        ActorIntegrationRecorder actorRecorder) : IZLinkActorFactory
    {
        public async ValueTask<IZLinkActor> CreateAsync(
            string actorId,
            IZLinkActorContext context,
            CancellationToken cancellationToken = default)
        {
            Interlocked.Increment(ref factoryRecorder.CreateCount);
            factoryRecorder.FirstFactoryCall.TrySetResult();
            await factoryRecorder.ReleaseFactory.Task.WaitAsync(cancellationToken);
            return new TestActor(actorId, context, actorRecorder);
        }
    }

    public sealed class ConcurrentActorFactoryRecorder
    {
        public int CreateCount;

        public TaskCompletionSource FirstFactoryCall { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ReleaseFactory { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    public sealed class ActorIntegrationRecorder
    {
        public ConcurrentQueue<string> DispatchBodies { get; } = new();

        public ConcurrentQueue<string> DispatchRooms { get; } = new();

        public ConcurrentQueue<string> DispatchSpotRids { get; } = new();

        public ConcurrentQueue<string> ChannelReplies { get; } = new();

        public ConcurrentQueue<string> ScopeViolations { get; } = new();

        public ConcurrentQueue<string> SpotActorJoins { get; } = new();

        public ConcurrentQueue<string> SpotActorLeaves { get; } = new();

        public volatile bool ConcurrentViolation;

        public int DisconnectCount;
    }

    public sealed record StageBootCommand(string ScopeId);

    private enum SpotRouteTransportKind
    {
        ClientServer,
        RouteMesh
    }

    public sealed record SpotRouteTargetCommand(string Value);

    public sealed record SpotRouteTargetRequest(string Value);

    public sealed record SpotRouteTargetReply(string Value);

    public sealed record SpotRouteSendCallerCommand(string Value);

    public sealed record SpotRouteRequestCallerCommand(string Value);

    public sealed class SpotRouteTargetSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddPacket<SpotRouteTargetCommandHandler>();
            Context.AddPacket<SpotRouteTargetRequestHandler>();
        }
    }

    public sealed class SpotRouteCallerEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
    {
        public IZLinkEntrySpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddPacket<SpotRouteSendCallerHandler>();
            Context.AddPacket<SpotRouteRequestCallerHandler>();
        }
    }

    public sealed class SpotRouteTargetCommandHandler(SpotRouteTransportRecorder recorder)
        : IZLinkSpotPacketHandler<SpotRouteTargetSpot, SpotRouteTargetCommand>
    {
        public ValueTask HandleAsync(
            SpotRouteTargetSpot spot,
            SpotRouteTargetCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.Commands.Enqueue(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class SpotRouteTargetRequestHandler(SpotRouteTransportRecorder recorder)
        : IZLinkSpotRequestHandler<SpotRouteTargetSpot, SpotRouteTargetRequest, SpotRouteTargetReply>
    {
        public ValueTask<SpotRouteTargetReply> HandleAsync(
            SpotRouteTargetSpot spot,
            SpotRouteTargetRequest request,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.Requests.Enqueue(request.Value);
            return ValueTask.FromResult(new SpotRouteTargetReply($"reply:{request.Value}"));
        }
    }

    public sealed class SpotRouteSendCallerHandler(IZLinkSpotClient spotClient)
        : IZLinkSpotPacketHandler<SpotRouteCallerEntrySpot, SpotRouteSendCallerCommand>
    {
        public async ValueTask HandleAsync(
            SpotRouteCallerEntrySpot spot,
            SpotRouteSendCallerCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            await spotClient.SendSpot("route-target", new SpotRouteTargetCommand(message.Value))
                .Submit(cancellationToken)
                .ConfigureAwait(false);
        }
    }

    public sealed class SpotRouteRequestCallerHandler(
        IZLinkSpotClient spotClient,
        SpotRouteTransportRecorder recorder)
        : IZLinkSpotPacketHandler<SpotRouteCallerEntrySpot, SpotRouteRequestCallerCommand>
    {
        public async ValueTask HandleAsync(
            SpotRouteCallerEntrySpot spot,
            SpotRouteRequestCallerCommand message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            var reply = await spotClient
                .RequestSpot("route-target", new SpotRouteTargetRequest(message.Value))
                .Timeout(TimeSpan.FromMilliseconds(500))
                .SubmitAsync<SpotRouteTargetReply>(cancellationToken)
                .ConfigureAwait(false);
            recorder.Replies.Enqueue(reply.Value);
        }
    }

    public sealed class FixedSpotRouteResolver : IZLinkSpotRouteResolver
    {
        private ZLinkSpotRoute? _route;

        public void Configure(
            string routerChannelId,
            RoutingId targetNodeRid,
            RoutingId spotRid)
        {
            _route = new ZLinkSpotRoute(routerChannelId, targetNodeRid, spotRid);
        }

        public ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
            string spotName,
            CancellationToken cancellationToken)
        {
            _ = spotName;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(RequireRoute());
        }

        public ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
            RoutingId spotRid,
            CancellationToken cancellationToken)
        {
            _ = spotRid;
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(RequireRoute());
        }

        private ZLinkSpotRoute RequireRoute()
        {
            return _route ?? throw new InvalidOperationException("SPOT route is not configured.");
        }
    }

    public sealed class SpotRouteTransportRecorder
    {
        public ConcurrentQueue<string> Commands { get; } = new();

        public ConcurrentQueue<string> Requests { get; } = new();

        public ConcurrentQueue<string> Replies { get; } = new();
    }

    public sealed record ActorContextChannelReq(string Value);

    public sealed record ActorContextChannelRes(string Value);

    public sealed class ActorContextChannelHandler
    {
        [ZLinkRequest]
        public ValueTask<ActorContextChannelRes> HandleAsync(
            ActorContextChannelReq request,
            ZLinkRequestContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            return ValueTask.FromResult(new ActorContextChannelRes(request.Value));
        }
    }

    public sealed class StageOrdersHandler(OrdersRecorder recorder)
    {
        [ZLinkSend]
        public ValueTask HandleAsync(
            StageBootCommand request,
            ZLinkSendContext context,
            CancellationToken cancellationToken)
        {
            _ = context;
            _ = cancellationToken;
            recorder.ReceivedScopes.Add(request.ScopeId);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class SpotScopeMarker
    {
        public string Id { get; } = Guid.NewGuid().ToString("N");
    }

    public sealed class OrdersRecorder
    {
        public ConcurrentBag<string> ReceivedScopes { get; } = [];
    }

    public sealed class SpotCreatePayloadRecorder
    {
        private readonly object _gate = new();
        private TaskCompletionSource? _entered;
        private TaskCompletionSource? _release;

        public ConcurrentBag<IReadOnlyList<string>> Payloads { get; } = [];

        public void BlockCreate()
        {
            lock (_gate)
            {
                _entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            }
        }

        public Task WaitCreateEnteredAsync()
        {
            lock (_gate)
            {
                return _entered?.Task ?? Task.CompletedTask;
            }
        }

        public void ReleaseCreate()
        {
            lock (_gate)
            {
                _release?.TrySetResult();
            }
        }

        public async ValueTask RecordAsync(
            IReadOnlyList<Message> createParts,
            CancellationToken cancellationToken)
        {
            Task? release;
            lock (_gate)
            {
                _entered?.TrySetResult();
                release = _release?.Task;
            }

            if (release is not null)
            {
                await release.WaitAsync(cancellationToken);
            }

            Payloads.Add(createParts.Select(static part => part.GetString()).ToArray());
        }
    }

    public sealed class SpotLifecycleRecorder
    {
        public ConcurrentBag<string> LocalEvents { get; } = [];

        public ConcurrentBag<string> ExternalEvents { get; } = [];

        private int _tickCount;

        public int TickCount => Volatile.Read(ref _tickCount);

        public void RecordTick()
        {
            Interlocked.Increment(ref _tickCount);
        }
    }

    public sealed class SpotEventsRecorder
    {
        private readonly ConcurrentDictionary<global::Systems.Zlink.RoutingId, string> _scopes = [];
        private readonly ConcurrentBag<global::Systems.Zlink.RoutingId> _closing = [];

        public ConcurrentDictionary<global::Systems.Zlink.RoutingId, string> Initialized => _scopes;

        public ConcurrentBag<global::Systems.Zlink.RoutingId> Closing => _closing;

        public void RecordInitialized(global::Systems.Zlink.RoutingId spotRid, string scopeId)
        {
            _scopes[spotRid] = scopeId;
        }

        public void RecordClosing(global::Systems.Zlink.RoutingId spotRid)
        {
            _closing.Add(spotRid);
        }

        public string? ScopeId(global::Systems.Zlink.RoutingId spotRid)
        {
            return _scopes.TryGetValue(spotRid, out var scopeId) ? scopeId : null;
        }
    }

    public sealed class PublishingStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
        {
            _ = await Context.AddTimer<SpotHeartbeatTimerHandler>(
                "heartbeat",
                TimeSpan.FromMilliseconds(250),
                cancellationToken: cancellationToken);
        }
    }

    public sealed class TimerFailureProbe : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
    {
        private readonly TaskCompletionSource<ZLinkSpotEvent> _completion =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public ValueTask HandleAsync(
            ZLinkSpotEvent @event,
            CancellationToken cancellationToken)
        {
            _ = cancellationToken;
            if (@event.Event == ZLinkSpotEventKind.TimerHandlerFailed)
            {
                _completion.TrySetResult(@event);
            }

            return ValueTask.CompletedTask;
        }

        public async Task<ZLinkSpotEvent> WaitAsync(TimeSpan timeout)
        {
            using var timeoutSource = new CancellationTokenSource(timeout);
            return await _completion.Task.WaitAsync(timeoutSource.Token);
        }
    }

    public sealed class LocalSubscriberStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<LocalStageEventHandler>("stage.local");
        }
    }

    public sealed class ExternalSubscriberStageSpot(IZLinkSpotContext context) : IZLinkSpot
    {
        public IZLinkSpotContext Context { get; } = context;

        public void Configure()
        {
            Context.AddSubscribe<ExternalStageEventHandler>("stage.external");
        }
    }

    public sealed record LocalStageEvent(string SpotRid);

    public sealed record ExternalStageEvent(string Value);

    public sealed class SpotHeartbeatTimerHandler(
        SpotLifecycleRecorder recorder,
        IZLinkSpotClient spotClient)
        : IZLinkSpotTimerHandler<PublishingStageSpot>
    {
        public async ValueTask HandleAsync(
            PublishingStageSpot spot,
            ZLinkTimerTick tick,
            CancellationToken cancellationToken)
        {
            _ = tick;
            recorder.RecordTick();
            await spotClient.Publish("stage.local", new LocalStageEvent(spot.Context.SpotRid.ToString()))
                .Submit(cancellationToken);
        }
    }

    public sealed class LocalStageEventHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotSubscriptionHandler<LocalSubscriberStageSpot, LocalStageEvent>
    {
        public ValueTask HandleAsync(
            LocalSubscriberStageSpot spot,
            LocalStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.LocalEvents.Add(message.SpotRid);
            return ValueTask.CompletedTask;
        }
    }

    public sealed class ExternalStageEventHandler(SpotLifecycleRecorder recorder)
        : IZLinkSpotSubscriptionHandler<ExternalSubscriberStageSpot, ExternalStageEvent>
    {
        public ValueTask HandleAsync(
            ExternalSubscriberStageSpot spot,
            ExternalStageEvent message,
            CancellationToken cancellationToken)
        {
            _ = spot;
            _ = cancellationToken;
            recorder.ExternalEvents.Add(message.Value);
            return ValueTask.CompletedTask;
        }
    }

    private static ZLinkSpotActivation GetSingleSpotActivation(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName)
    {
        var nodeRuntime = GetSpotNodeRuntime(runtime, spotNodeName);
        return Assert.Single(nodeRuntime.Spots);
    }

    private static ZLinkSpotNodeRuntime GetSpotNodeRuntime(
        ZLinkFrameworkRuntime runtime,
        string spotNodeName)
    {
        return runtime.GetSpotNodeRuntime(spotNodeName);
    }

    private static string GetSubscriptionPumpState(ZLinkSpotActivation activation)
    {
        return activation.SubscriptionPumpState;
    }

    private static async Task SubmitEntrySpotStringAsync(
        ZLinkFrameworkRuntime actorRuntime,
        IZLinkActor actor,
        string packetName,
        string value)
    {
        using var body = global::Systems.Zlink.Message.FromString(value);
        await actorRuntime.SubmitActorAsync(
                actor,
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Raw,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    packetName,
                    ZlinkStreamMetadata.Empty),
                body)
            .ConfigureAwait(false);
    }

    private static ZLinkEnvelopeHeader CreateEntrySpotEnvelopeHeader<TMessage>(
        string channelName,
        TMessage message)
    {
        return new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Command,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(message)
                ?? throw new InvalidOperationException("Message name is required."),
            ZLinkEnvelopeCodec.DefaultContentType,
            null,
            null,
            null,
            null,
            null);
    }

    private static ZLinkBackendActorPart CreateEntryActorHeaderPart(
        IZLinkActor actor,
        string packetName)
    {
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            ZlinkStreamCodec.Raw,
            ZlinkStreamHeaderFlags.None,
            null,
            packetName,
            ZlinkStreamMetadata.Empty);

        return new ZLinkBackendActorPart(
            new ZLinkBackendActorRef(RoutingId.FromString("01"), actor.ActorId, 0),
            RoutingId.FromString("02"),
            RoutingId.FromString("03"),
            Message.FromBytes(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span),
            More: true);
    }

    private static ZLinkBackendActorPart CreateEntryActorBodyPart(
        IZLinkActor actor,
        string value)
    {
        return new ZLinkBackendActorPart(
            new ZLinkBackendActorRef(RoutingId.FromString("01"), actor.ActorId, 0),
            RoutingId.FromString("02"),
            RoutingId.FromString("03"),
            Message.FromString(value),
            More: false);
    }
}
