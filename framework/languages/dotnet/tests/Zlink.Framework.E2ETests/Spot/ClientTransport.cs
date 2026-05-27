using System.Text;
using Microsoft.Extensions.DependencyInjection;

namespace Zlink.Framework.E2ETests.Spot;


public sealed class ClientTransportTests : SpotTestSupport
{
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
            var resolver = host.Services.GetRequiredService<FixedSpotRemoteAddressResolver>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            var target = await manager.GetOrCreateAsync<SpotRouteTargetSpot>(
                RoutingId.From(Encoding.UTF8.GetBytes("spotapi1")));
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
            var resolver = host.Services.GetRequiredService<FixedSpotRemoteAddressResolver>();
            var recorder = host.Services.GetRequiredService<SpotRouteTransportRecorder>();
            var nodeRuntime = runtime.GetSpotNodeRuntime("route-target-node");
            var target = await manager.GetOrCreateAsync<SpotRouteTargetSpot>(
                RoutingId.From(Encoding.UTF8.GetBytes("spotapi2")));
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
}
