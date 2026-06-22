using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

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
