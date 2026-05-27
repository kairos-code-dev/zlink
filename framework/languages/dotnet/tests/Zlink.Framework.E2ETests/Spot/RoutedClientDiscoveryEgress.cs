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


public sealed class RoutedClientDiscoveryEgressTests : SpotTestSupport
{
    [Fact]
    public async Task RoutedSpotClient_SendSpot_UsesDiscoveryRouteMeshEgressPeer()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var gatewayRouteEndpoint = GetFreeTcpEndpoint();
        var playRouteEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var targetSpotRid = RoutingId.From(Encoding.UTF8.GetBytes("egress-spot-03"));

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var targetBuilder = Host.CreateApplicationBuilder();
        targetBuilder.Services.AddSingleton<SpotRouteTransportRecorder>();
        targetBuilder.Services.AddScoped<SpotRouteTargetCommandHandler>();
        targetBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddRouteMeshChannel("play.route", channel =>
            {
                channel.Bind(playRouteEndpoint);
                channel.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.From(
                        Encoding.UTF8.GetBytes("egress-target-03"));
                });
            });
            options.AddSpotMesh("spot.route.discovery.egress", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("route-target-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(GetFreeTcpEndpoint());
                    router.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.From(
                            Encoding.UTF8.GetBytes("egress-node-03"));
                    });
                });
                spot.AcceptSpotRoutesFromChannel(
                    "play.route",
                    routes => routes.UseManualConnections(
                        peers => peers.Connect(playRouteEndpoint)));
                spot.AddSpotFactory<SpotRouteTargetSpot>();
            });
            });
        });

        var sourceBuilder = Host.CreateApplicationBuilder();
        sourceBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
            options.AddRouteMeshChannel("gateway.route", channel =>
            {
                channel.Bind(gatewayRouteEndpoint);
                channel.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.From(
                        Encoding.UTF8.GetBytes("egress-gateway-03"));
                });
                channel.UseManualConnections(peers => peers.Connect(playRouteEndpoint));
                channel.EnableSpotRouteEgress("play.route");
            });
        });

        using var registryHost = registryBuilder.Build();
        using var targetHost = targetBuilder.Build();
        using var sourceHost = sourceBuilder.Build();

        await registryHost.StartAsync();
        await targetHost.StartAsync();
        await sourceHost.StartAsync();

        try
        {
            var manager = targetHost.Services.GetRequiredService<IZLinkSpotManager>();
            var spots = sourceHost.Services.GetRequiredService<IZLinkRoutedSpotClient>();
            var runtime = targetHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = targetHost.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            _ = await manager.GetOrCreateAsync<SpotRouteTargetSpot>(targetSpotRid);
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play.route");

            await RetryAsync(
                async () =>
                {
                    await spots
                        .ViaEgressChannel("gateway.route")
                        .SendSpot(targetSpotRid, new SpotRouteTargetCommand("route-egress-discovery-send"))
                        .Submit(CancellationToken.None)
                        .ConfigureAwait(false);
                    return recorder.Commands.Contains("route-egress-discovery-send");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sourceHost, targetHost, registryHost);
        }
    }
}
