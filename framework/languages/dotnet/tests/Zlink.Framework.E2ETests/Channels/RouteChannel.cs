using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Runtime.Host;

namespace Zlink.Framework.E2ETests.Channels;

[Collection(nameof(FrameworkTestsCollection))]
public sealed class RouteChannelTests
{
    [Fact]
    public async Task RouteRequest_WorksAcrossDiscoveryAttachedRouters()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var rightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.From("11");
        var rightRid = RoutingId.From("22");

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            {
                var routed = options.AddRouteMeshChannel("backend.discovery");
                routed.EnableServer(leftEndpoint);
                routed.SetRoutingId(leftRid);

            }
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            {
                var routed = options.AddRouteMeshChannel("backend.discovery");
                routed.EnableServer(rightEndpoint);
                routed.SetRoutingId(rightRid);
                routed.AddRequestHandler<SharedPacketRouteHandler, SharedPacketRequest, SharedPacketReply>(
                    "SharedPacket");

            }
        });

        using var registryHost = registryBuilder.Build();
        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await registryHost.StartAsync();
            await rightHost.StartAsync();
            await leftHost.StartAsync();

            var client = leftHost.Services.GetRequiredService<IZLinkRouteClient>();
            var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client.Request("backend.discovery", rightRid, new SharedPacketRequest("discovery", 1))
                    .PacketName("SharedPacket")
                    .Timeout(TimeSpan.FromSeconds(1))
                    .Async<SharedPacketReply>(),
                static result => result.Value == "discovery",
                attempts: 30,
                delayMs: 100);

            Assert.Equal("discovery", reply.Value);
        }, leftHost, rightHost, registryHost);
    }

    [Fact]
    public async Task RouteRequest_DiscoveryAttachedRouters_FirstRequestAfterStart_ReachesPeer()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var rightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.From("71");
        var rightRid = RoutingId.From("72");

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            var routed = options.AddRouteMeshChannel("backend.first");
            routed.EnableServer(leftEndpoint);
            routed.SetRoutingId(leftRid);
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            var routed = options.AddRouteMeshChannel("backend.first");
            routed.EnableServer(rightEndpoint);
            routed.SetRoutingId(rightRid);
            routed.AddRequestHandler<SharedPacketRouteHandler, SharedPacketRequest, SharedPacketReply>(
                "SharedPacket");
        });

        using var registryHost = registryBuilder.Build();
        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await registryHost.StartAsync();
            await rightHost.StartAsync();
            await leftHost.StartAsync();

            var client = leftHost.Services.GetRequiredService<IZLinkRouteClient>();
            var reply = await client.Request(
                    "backend.first",
                    rightRid,
                    new SharedPacketRequest("first", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();

            Assert.Equal("first", reply.Value);
        }, leftHost, rightHost, registryHost);
    }

    [Fact]
    public async Task RouteRequest_DiscoveryAttachedRouters_MultiplePeers_TargetedRequestsReachEachNode()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var sessionAEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var sessionBEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var playAEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var playBEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var sessionARid = RoutingId.From("1105");
        var sessionBRid = RoutingId.From("1106");
        var playARid = RoutingId.From("2201");
        var playBRid = RoutingId.From("2202");

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        using var registryHost = registryBuilder.Build();
        using var sessionAHost = BuildRoutePeer(sessionAEndpoint, sessionARid, handle: false);
        using var sessionBHost = BuildRoutePeer(sessionBEndpoint, sessionBRid, handle: false);
        using var playAHost = BuildRoutePeer(playAEndpoint, playARid, handle: true);
        using var playBHost = BuildRoutePeer(playBEndpoint, playBRid, handle: true);

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await registryHost.StartAsync();
            await playAHost.StartAsync();
            await playBHost.StartAsync();
            await sessionAHost.StartAsync();
            await sessionBHost.StartAsync();

            var sessionAClient = sessionAHost.Services.GetRequiredService<IZLinkRouteClient>();
            var sessionBClient = sessionBHost.Services.GetRequiredService<IZLinkRouteClient>();

            var replyA = await sessionAClient.Request(
                    "backend.multi",
                    playARid,
                    new SharedPacketRequest("play-a", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();
            var replyB = await sessionBClient.Request(
                    "backend.multi",
                    playBRid,
                    new SharedPacketRequest("play-b", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();

            Assert.Equal("play-a", replyA.Value);
            Assert.Equal("play-b", replyB.Value);
        }, sessionBHost, sessionAHost, playBHost, playAHost, registryHost);

        IHost BuildRoutePeer(string endpoint, RoutingId rid, bool handle)
        {
            var builder = Host.CreateApplicationBuilder();
            builder.Services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
                var routed = options.AddRouteMeshChannel("backend.multi");
                routed.EnableServer(endpoint);
                routed.SetRoutingId(rid);
                if (handle)
                {
                    routed.AddRequestHandler<SharedPacketRouteHandler, SharedPacketRequest, SharedPacketReply>(
                        "SharedPacket");
                }
            });
            return builder.Build();
        }
    }

    [Fact]
    public async Task RouteRequest_DiscoveryAttachedRouters_SixPeerMesh_TargetedRequestsReachMiddleNodes()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        using var registryHost = registryBuilder.Build();
        using var sessionAHost = BuildRoutePeer(RoutingId.From("1105"), handle: false);
        using var sessionBHost = BuildRoutePeer(RoutingId.From("1106"), handle: false);
        using var playAHost = BuildRoutePeer(RoutingId.From("2201"), handle: true);
        using var playBHost = BuildRoutePeer(RoutingId.From("2202"), handle: true);
        using var apiAHost = BuildRoutePeer(RoutingId.From("3301"), handle: false);
        using var apiBHost = BuildRoutePeer(RoutingId.From("3302"), handle: false);

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await registryHost.StartAsync();
            await apiAHost.StartAsync();
            await apiBHost.StartAsync();
            await playAHost.StartAsync();
            await playBHost.StartAsync();
            await sessionAHost.StartAsync();
            await sessionBHost.StartAsync();

            var sessionAClient = sessionAHost.Services.GetRequiredService<IZLinkRouteClient>();
            var sessionBClient = sessionBHost.Services.GetRequiredService<IZLinkRouteClient>();

            var replyA = await sessionAClient.Request(
                    "backend.six",
                    RoutingId.From("2201"),
                    new SharedPacketRequest("play-a", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();
            var replyB = await sessionBClient.Request(
                    "backend.six",
                    RoutingId.From("2202"),
                    new SharedPacketRequest("play-b", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();

            Assert.Equal("play-a", replyA.Value);
            Assert.Equal("play-b", replyB.Value);
        }, sessionBHost, sessionAHost, playBHost, playAHost, apiBHost, apiAHost, registryHost);

        IHost BuildRoutePeer(RoutingId rid, bool handle)
        {
            var builder = Host.CreateApplicationBuilder();
            builder.Services.AddZLinkFramework(options =>
            {
                options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
                var routed = options.AddRouteMeshChannel("backend.six");
                routed.EnableServer($"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}");
                routed.SetRoutingId(rid);
                if (handle)
                {
                    routed.AddRequestHandler<SharedPacketRouteHandler, SharedPacketRequest, SharedPacketReply>(
                        "SharedPacket");
                }
            });
            return builder.Build();
        }
    }

    [Fact]
    public async Task RouteMeshChannel_WithAcceptedSpotRoutes_DoesNotAdvertiseDuplicateRoutingId()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var observerEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var targetRouteEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var targetSpotRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var observerRid = RoutingId.From("7101");
        var targetRid = RoutingId.From("7201");

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var observerBuilder = Host.CreateApplicationBuilder();
        observerBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            var routed = options.AddRouteMeshChannel("backend.accepted");
            routed.EnableServer(observerEndpoint);
            routed.SetRoutingId(observerRid);
        });

        var targetBuilder = Host.CreateApplicationBuilder();
        targetBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            var routed = options.AddRouteMeshChannel("backend.accepted");
            routed.EnableServer(targetRouteEndpoint);
            routed.SetRoutingId(targetRid);
            routed.AddRequestHandler<SharedPacketRouteHandler, SharedPacketRequest, SharedPacketReply>(
                "SharedPacket");

            var mesh = options.AddSpotMesh("spot.accepted");
            var spot = mesh.AddNode("accepted-target-node");
            spot.EnableRouter(targetSpotRouterEndpoint);
            spot.SetRouterRoutingId(targetRid);
            spot.AcceptSpotRoutesFromChannel("backend.accepted");
        });

        using var registryHost = registryBuilder.Build();
        using var observerHost = observerBuilder.Build();
        using var targetHost = targetBuilder.Build();

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await registryHost.StartAsync();
            await targetHost.StartAsync();
            await observerHost.StartAsync();

            var observerRuntime = observerHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var route = observerRuntime.GetRouteChannel("backend.accepted");
            var sawSingleTargetPeer = await WaitUntilAsync(() =>
            {
                var peers = route.Discovery?.MemberPeers() ?? [];
                return peers.Count(peer =>
                    peer.ChannelName == "backend.accepted"
                    && peer.ServiceRole == ZLinkServiceRole.Router
                    && peer.RoutingId == targetRid) == 1;
            });

            Assert.True(sawSingleTargetPeer);

            var client = observerHost.Services.GetRequiredService<IZLinkRouteClient>();
            var reply = await client.Request(
                    "backend.accepted",
                    targetRid,
                    new SharedPacketRequest("accepted", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();

            Assert.Equal("accepted", reply.Value);
        }, observerHost, targetHost, registryHost);
    }

    [Fact]
    public async Task RouteRequest_MatchesRepliesByRequestSequenceWhenPacketNameIsShared()
    {
        var leftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var rightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.From("01");
        var rightRid = RoutingId.From("02");

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var routed = options.AddRouteMeshChannel("backend");
                routed.EnableServer(leftEndpoint);
                routed.ConfigureSocket().SendTimeout = TimeSpan.FromSeconds(10);
                routed.SetRoutingId(leftRid);
                routed.EnableClient(rightEndpoint);

            }
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var routed = options.AddRouteMeshChannel("backend");
                routed.EnableServer(rightEndpoint);
                routed.ConfigureSocket().SendTimeout = TimeSpan.FromSeconds(10);
                routed.SetRoutingId(rightRid);
                routed.EnableClient(leftEndpoint);
                routed.AddRequestHandler<SharedPacketRouteHandler, SharedPacketRequest, SharedPacketReply>(
                    "SharedPacket");

            }
        });

        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await rightHost.StartAsync();
            await leftHost.StartAsync();

            var client = leftHost.Services.GetRequiredService<IZLinkRouteClient>();
            _ = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client.Request("backend", rightRid, new SharedPacketRequest("warmup", 1))
                    .PacketName("SharedPacket")
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<SharedPacketReply>(),
                static result => result.Value == "warmup",
                attempts: 30,
                delayMs: 100);

            var slow = client.Request("backend", rightRid, new SharedPacketRequest("slow", 40))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();
            var fast = client.Request("backend", rightRid, new SharedPacketRequest("fast", 1))
                .PacketName("SharedPacket")
                .Timeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>();

            var replies = await Task.WhenAll(slow.AsTask(), fast.AsTask());

            Assert.Equal("slow", replies[0].Value);
            Assert.Equal("fast", replies[1].Value);
        }, leftHost, rightHost);
    }

    [Fact]
    public async Task RouteRequest_UsesMappedHandlerGroup()
    {
        var leftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var rightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.From("31");
        var rightRid = RoutingId.From("32");

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            {
                var routed = options.AddRouteMeshChannel("backend.group");
                routed.EnableServer(leftEndpoint);
                routed.SetRoutingId(leftRid);
                routed.EnableClient(rightEndpoint);

            }
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddHandlersFromAssemblyOf<RouteChannelTests>();
            {
                var routed = options.AddRouteMeshChannel("backend.group");
                routed.EnableServer(rightEndpoint);
                routed.SetRoutingId(rightRid);
                routed.EnableClient(leftEndpoint);
                routed.AddHandlerGroup("route-shared");

            }
        });

        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await ChannelMessagingTestSupport.RunWithHostCleanupAsync(async () =>
        {
            await rightHost.StartAsync();
            await leftHost.StartAsync();

            var client = leftHost.Services.GetRequiredService<IZLinkRouteClient>();
            var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
                async () => await client.Request("backend.group", rightRid, new SharedPacketRequest("group", 1))
                    .Timeout(TimeSpan.FromSeconds(3))
                    .Async<SharedPacketReply>(),
                static result => result.Value == "group",
                attempts: 30,
                delayMs: 100);

            Assert.Equal("group", reply.Value);
        }, leftHost, rightHost);
    }

    public sealed record SharedPacketRequest(string Value, int DelayMs);

    public sealed record SharedPacketReply(string Value);

    private static async Task<bool> WaitUntilAsync(Func<bool> predicate)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTimeOffset.UtcNow < deadline)
        {
            if (predicate())
            {
                return true;
            }

            await Task.Delay(100);
        }

        return false;
    }

    [ZLinkHandlerGroup("route-shared")]
    public sealed class SharedPacketRouteHandler : IZLinkRouteRequestHandler<SharedPacketRequest, SharedPacketReply>
    {
        public async ValueTask<SharedPacketReply> HandleAsync(
            SharedPacketRequest request,
            ZLinkRouteRequestContext context,
            CancellationToken cancellationToken)
        {
            await Task.Delay(request.DelayMs, cancellationToken);
            return new SharedPacketReply(request.Value);
        }
    }
}
