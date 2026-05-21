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

namespace Zlink.Framework.E2ETests;


public sealed class RouteAcceptanceTests : SpotTestSupport
{
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
}
