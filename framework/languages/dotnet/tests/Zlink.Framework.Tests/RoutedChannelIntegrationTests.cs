using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.Tests;

[Collection("FrameworkRuntimeIntegrationTestsCollection")]
public sealed class RoutedChannelIntegrationTests
{
    [Fact]
    public async Task RoutedRequest_WorksAcrossDiscoveryAttachedRouters()
    {
        var registryPubEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var registryRouterEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var rightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.FromString("11");
        var rightRid = RoutingId.FromString("22");

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddRouteMeshChannel("backend.discovery", routed =>
            {
                routed.Bind(leftEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = leftRid);
            });
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddRouteMeshChannel("backend.discovery", routed =>
            {
                routed.Bind(rightEndpoint);
                routed.ConfigureRouting(routing => routing.RoutingId = rightRid);
                routed.AddRequestHandler<SharedPacketRoutedHandler, SharedPacketRequest, SharedPacketReply>(
                    "SharedPacket");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await registryHost.StartAsync();
        await rightHost.StartAsync();
        await leftHost.StartAsync();

        var client = leftHost.Services.GetRequiredService<IZLinkRoutedClient>();
        var reply = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.RequestTo("backend.discovery", rightRid, new SharedPacketRequest("discovery", 1))
                .WithPacketName("SharedPacket")
                .WithTimeout(TimeSpan.FromSeconds(1))
                .Async<SharedPacketReply>(),
            static result => result.Value == "discovery",
            attempts: 30,
            delayMs: 100);

        Assert.Equal("discovery", reply.Value);

        await ChannelMessagingTestSupport.StopHostsAsync(leftHost, rightHost, registryHost);
    }

    [Fact]
    public async Task RoutedRequest_MatchesRepliesByRequestSequenceWhenPacketNameIsShared()
    {
        var leftEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var rightEndpoint = $"tcp://127.0.0.1:{ChannelMessagingTestSupport.GetEphemeralPort()}";
        var leftRid = RoutingId.FromString("01");
        var rightRid = RoutingId.FromString("02");

        var leftBuilder = Host.CreateApplicationBuilder();
        leftBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddRouteMeshChannel("backend", routed =>
            {
                routed.Bind(leftEndpoint);
                routed.ConfigureSocket(socket =>
                    socket.SendTimeout = TimeSpan.FromSeconds(10));
                routed.ConfigureRouting(routing => routing.RoutingId = leftRid);
                routed.UseManualConnections(peers => peers.Connect(rightEndpoint));
            });
        });

        var rightBuilder = Host.CreateApplicationBuilder();
        rightBuilder.Services.AddZLinkFramework(options =>
        {
            options.AddRouteMeshChannel("backend", routed =>
            {
                routed.Bind(rightEndpoint);
                routed.ConfigureSocket(socket =>
                    socket.SendTimeout = TimeSpan.FromSeconds(10));
                routed.ConfigureRouting(routing => routing.RoutingId = rightRid);
                routed.UseManualConnections(peers => peers.Connect(leftEndpoint));
                routed.AddRequestHandler<SharedPacketRoutedHandler, SharedPacketRequest, SharedPacketReply>(
                    "SharedPacket");
            });
        });

        using var leftHost = leftBuilder.Build();
        using var rightHost = rightBuilder.Build();

        await rightHost.StartAsync();
        await leftHost.StartAsync();

        var client = leftHost.Services.GetRequiredService<IZLinkRoutedClient>();
        _ = await ChannelMessagingTestSupport.ExecuteWithRetryAsync(
            async () => await client.RequestTo("backend", rightRid, new SharedPacketRequest("warmup", 1))
                .WithPacketName("SharedPacket")
                .WithTimeout(TimeSpan.FromSeconds(3))
                .Async<SharedPacketReply>(),
            static result => result.Value == "warmup",
            attempts: 30,
            delayMs: 100);

        var slow = client.RequestTo("backend", rightRid, new SharedPacketRequest("slow", 40))
            .WithPacketName("SharedPacket")
            .WithTimeout(TimeSpan.FromSeconds(3))
            .Async<SharedPacketReply>();
        var fast = client.RequestTo("backend", rightRid, new SharedPacketRequest("fast", 1))
            .WithPacketName("SharedPacket")
            .WithTimeout(TimeSpan.FromSeconds(3))
            .Async<SharedPacketReply>();

        var replies = await Task.WhenAll(slow.AsTask(), fast.AsTask());

        Assert.Equal("slow", replies[0].Value);
        Assert.Equal("fast", replies[1].Value);

        await ChannelMessagingTestSupport.StopHostsAsync(leftHost, rightHost);
    }

    public sealed record SharedPacketRequest(string Value, int DelayMs);

    public sealed record SharedPacketReply(string Value);

    public sealed class SharedPacketRoutedHandler : IZLinkRoutedRequestHandler<SharedPacketRequest, SharedPacketReply>
    {
        public async ValueTask<SharedPacketReply> HandleAsync(
            SharedPacketRequest request,
            ZLinkRoutedRequestContext context,
            CancellationToken cancellationToken)
        {
            await Task.Delay(request.DelayMs, cancellationToken);
            return new SharedPacketReply(request.Value);
        }
    }
}
