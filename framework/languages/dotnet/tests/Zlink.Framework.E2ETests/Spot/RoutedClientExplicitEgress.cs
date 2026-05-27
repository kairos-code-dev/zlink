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


public sealed class RoutedClientExplicitEgressTests : SpotTestSupport
{
    [Fact]
    public async Task RoutedSpotClient_RequestSpot_UsesExplicitClientServerEgressChannel()
    {
        var apiEndpoint = GetFreeTcpEndpoint();
        var playRouteEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var targetSpotRid = RoutingId.FromBytes(Encoding.UTF8.GetBytes("egress-spot-01"));

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotRouteTransportRecorder>();
        builder.Services.AddScoped<RoutedSpotApiHandler>();
        builder.Services.AddScoped<SpotRouteTargetRequestHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddClientServerChannel("api", channel =>
            {
                channel.EnableServer(server => server.Bind(apiEndpoint));
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(peers => peers.Connect(apiEndpoint));
                });
                channel.AddRequestHandler<RoutedSpotApiHandler, RoutedSpotApiRequest, RoutedSpotApiReply>();
            });
            options.AddClientServerChannel("gateway.client", channel =>
            {
                channel.EnableClient(client =>
                {
                    client.UseManualConnections(peers => peers.Connect(playRouteEndpoint));
                });
                channel.EnableSpotRouteEgress("play.route");
            });
            options.AddClientServerChannel("play.route", channel =>
            {
                channel.EnableServer(server =>
                {
                    server.Bind(playRouteEndpoint);
                    server.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromBytes(
                            Encoding.UTF8.GetBytes("egress-target"));
                    });
                });
            });
            options.AddSpotMesh("spot.route.egress", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("route-target-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(GetFreeTcpEndpoint());
                    router.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromBytes(
                            Encoding.UTF8.GetBytes("egress-node"));
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

        using var host = builder.Build();
        await host.StartAsync();
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var client = host.Services.GetRequiredService<IZLinkChannelClient>();
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            _ = await manager.GetOrCreateAsync<SpotRouteTargetSpot>(targetSpotRid);
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play.route");

            var reply = await RetryAsync(
                async () => await client
                    .RequestChannel("api", new RoutedSpotApiRequest(targetSpotRid.ToBytes().ToArray(), "egress-request"))
                    .Timeout(TimeSpan.FromMilliseconds(500))
                    .SubmitAsync<RoutedSpotApiReply>(CancellationToken.None)
                    .ConfigureAwait(false),
                result => result.Value == "reply:egress-request",
                TimeSpan.FromSeconds(5));

            Assert.Equal("reply:egress-request", reply.Value);
            Assert.Contains("egress-request", recorder.Requests);
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task RoutedSpotClient_SendSpot_UsesExplicitRouteMeshEgressChannel()
    {
        var gatewayRouteEndpoint = GetFreeTcpEndpoint();
        var playRouteEndpoint = GetFreeTcpEndpoint();
        var spotNodeEndpoint = GetFreeTcpEndpoint();
        var targetSpotRid = RoutingId.FromBytes(Encoding.UTF8.GetBytes("egress-spot-02"));

        var builder = Host.CreateApplicationBuilder();
        builder.Services.AddSingleton<SpotRouteTransportRecorder>();
        builder.Services.AddScoped<SpotRouteTargetCommandHandler>();
        builder.Services.AddZLinkFramework(options =>
        {
            options.AddRouteMeshChannel("gateway.route", channel =>
            {
                channel.Bind(gatewayRouteEndpoint);
                channel.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.FromBytes(
                        Encoding.UTF8.GetBytes("egress-gateway"));
                });
                channel.UseManualConnections(peers => peers.Connect(playRouteEndpoint));
                channel.EnableSpotRouteEgress("play.route");
            });
            options.AddRouteMeshChannel("play.route", channel =>
            {
                channel.Bind(playRouteEndpoint);
                channel.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.FromBytes(
                        Encoding.UTF8.GetBytes("egress-target"));
                });
            });
            options.AddSpotMesh("spot.route.egress", mesh =>
            {
                mesh.UseDiscovery(_ => { });
                mesh.AddNode("route-target-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(GetFreeTcpEndpoint());
                    router.ConfigureRouting(routing =>
                    {
                        routing.RoutingId = RoutingId.FromBytes(
                            Encoding.UTF8.GetBytes("egress-node"));
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

        using var host = builder.Build();
        await host.StartAsync();
        try
        {
            var manager = host.Services.GetRequiredService<IZLinkSpotManager>();
            var spots = host.Services.GetRequiredService<IZLinkRoutedSpotClient>();
            var runtime = host.Services.GetRequiredService<ZLinkFrameworkRuntime>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            _ = await manager.GetOrCreateAsync<SpotRouteTargetSpot>(targetSpotRid);
            await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play.route");

            await RetryAsync(
                async () =>
                {
                    await spots
                        .ViaEgressChannel("gateway.route")
                        .SendSpot(targetSpotRid, new SpotRouteTargetCommand("route-egress-send"))
                        .Submit(CancellationToken.None)
                        .ConfigureAwait(false);
                    return recorder.Commands.Contains("route-egress-send");
                },
                static result => result,
                TimeSpan.FromSeconds(5));
        }
        finally
        {
            await host.StopAsync();
        }
    }
}
