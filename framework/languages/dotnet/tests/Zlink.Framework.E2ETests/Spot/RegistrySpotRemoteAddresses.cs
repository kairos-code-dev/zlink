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


public sealed class RegistrySpotRemoteAddressesTests : SpotTestSupport
{
    [Fact]
    public async Task RegistrySpotRemoteAddresses_RequestSend_By_Name()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
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

            options.UseRegistrySpotRemoteAddresses("spot-registry-request-send");
            options.AddRouteMeshChannel("play", route =>
            {
                route.Bind(routeChannelEndpoint);
                route.ConfigureRouting(routing =>
                {
                    routing.RoutingId = RoutingId.FromBytes(
                        Encoding.UTF8.GetBytes("registry-play-route"));
                });
            });
            options.AddSpotMesh(spotChannel, mesh =>
            {
                mesh.AddNode("route-target-node", spot =>
            {
                spot.EnableRouter(router =>
                {
                    router.SetRouterBind(GetFreeTcpEndpoint());
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
        });

        using var registryHost = registryBuilder.Build();
        using var frameworkHost = frameworkBuilder.Build();
        await registryHost.StartAsync();
        await frameworkHost.StartAsync();

        var manager = frameworkHost.Services.GetRequiredService<IZLinkSpotManager>();
        var runtime = frameworkHost.Services.GetRequiredService<ZLinkFrameworkRuntime>();
        var resolver = frameworkHost.Services.GetRequiredService<IZLinkSpotRemoteAddressResolver>();
        var recorder = frameworkHost.Services.GetRequiredService<SpotRouteTransportRecorder>();
        var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
        _ = await manager.CreateAsync("route-target");
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play");
        var route = await RetryAsync(
            () => resolver.ResolveSpotRemoteAddressAsync("route-target", CancellationToken.None).AsTask(),
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
}
