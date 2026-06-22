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
        var spotRouterEndpoint = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            {
                var channel = options.AddClientServerChannel("api");
                channel.EnableServer(channelEndpoint);

            }
            {
                var mesh = options.AddSpotMesh("spot.route.client-server");
                {
                    var spot = mesh.AddNode("route-target-node");
                {
                    var router = spot.EnableRouter(spotRouterEndpoint);

                }
                                spot.AcceptSpotRoutesFromChannel("api", channelEndpoint);

                }

            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "api");
        var peer = Assert.Single(nodeRuntime.Node.Peers(), peer => peer.ChannelName == "api");
        Assert.Equal(ZLinkSpotPeerKind.RouterChannel, peer.Kind);
        Assert.Equal(ZLinkSpotPeerSource.Manual, peer.Source);

        await host.StopAsync();
    }

    [Fact]
    public async Task AcceptSpotRoutesFromChannel_RouteMesh_ManualConnections_AreApplied()
    {
        var routeEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var spotRouterEndpoint = GetFreeTcpEndpoint();

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddZLinkFramework(options =>
        {
            {
                var routed = options.AddRouteMeshChannel("play");
                routed.EnableServer(routeEndpoint);
                routed.SetRoutingId(RoutingId.From("aabbcc02"));

            }
            {
                var mesh = options.AddSpotMesh("spot.route.mesh");
                {
                    var spot = mesh.AddNode("route-target-node");
                {
                    var router = spot.EnableRouter(spotRouterEndpoint);

                }
                                spot.AcceptSpotRoutesFromChannel("play", routeEndpoint);

                }

            }
        });

        using var host = builder.Build();
        await host.StartAsync();

        var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play");
        var peer = Assert.Single(nodeRuntime.Node.Peers(), peer => peer.ChannelName == "play");
        Assert.Equal(ZLinkSpotPeerKind.RouterChannel, peer.Kind);
        Assert.Equal(ZLinkSpotPeerSource.Manual, peer.Source);

        await host.StopAsync();
    }

    [Fact]
    public async Task AddSpotMesh_AcceptSpotRoutesFromChannel_ClientServer_AllowsRouterSendToSpot()
    {
        await VerifyAcceptedRouteChannelSendToSpotAsync(
            SpotRouteTransportKind.ClientServer,
            "api");
    }

    [Fact]
    public async Task AddSpotMesh_AcceptSpotRoutesFromChannel_RouteMesh_AllowsRouterSendToSpot()
    {
        await VerifyAcceptedRouteChannelSendToSpotAsync(
            SpotRouteTransportKind.RouteMesh,
            "play");
    }

}
