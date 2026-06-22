using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.E2ETests.Channels;
using Zlink.Framework.Runtime.Backend.Contracts;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.E2ETests;


public sealed class RegistrySpotRemoteAddressesTests : SpotTestSupport
{
    [Fact(DisplayName = "DERR-004 Spot route missing request replies error and reports observer")]
    public async Task RegistrySpotRemoteAddresses_RequestSend_By_Rid()
    {
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var routeChannelEndpoint = GetFreeTcpEndpoint();
        var spotChannel = $"spot.registry.request-send.{Guid.NewGuid():N}";
        var evidence = new ChannelDispatchErrorEvidence();
        var logPath = Path.Combine(Path.GetTempPath(), $"zlink-derr004-{Guid.NewGuid():N}.log");
        var logs = new ChannelDispatchLogSink(logPath);

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });

        var frameworkBuilder = Host.CreateApplicationBuilder();
        frameworkBuilder.Logging.ClearProviders();
        frameworkBuilder.Logging.AddProvider(new ChannelDispatchLogProvider(logs));
        frameworkBuilder.Services.AddSingleton(evidence);
        frameworkBuilder.Services.AddSingleton<SpotRouteTransportRecorder>();
        frameworkBuilder.Services.AddScoped<SpotRouteTargetCommandHandler>();
        frameworkBuilder.Services.AddScoped<SpotRouteTargetRequestHandler>();
        frameworkBuilder.Services.AddScoped<SpotRouteSendCallerHandler>();
        frameworkBuilder.Services.AddScoped<SpotRouteRequestCallerHandler>();
        frameworkBuilder.Services.AddZLinkFramework(options =>
        {
            options.UseDiscovery().AddRegistryEndpoint(registryRouterEndpoint);
            options.ConfigureDispatch().SetMessageDispatchErrorObserver<ChannelDispatchErrorObserver>();

            options.UseRegistrySpotRemoteAddresses("spot-registry-request-send").RouterChannelId = "play";
            {
                var route = options.AddRouteMeshChannel("play");
                route.EnableServer(routeChannelEndpoint);
                route.SetRoutingId(RoutingId.From(
                        Encoding.UTF8.GetBytes("registry-play-route")));

            }
            {
                var mesh = options.AddSpotMesh(spotChannel);
                {
                    var spot = mesh.AddNode("route-target-node");
                {
                    var router = spot.EnableRouter(GetFreeTcpEndpoint());
                    {
                        var routing = router.ConfigureRouterRouting();
                        routing.RoutingId = RoutingId.From(
                            Encoding.UTF8.GetBytes("registry-target-node"));

                    }

                }
                                spot.AcceptSpotRoutesFromChannel("play", routeChannelEndpoint);
                spot.AddEntrySpot<SpotRouteCallerEntrySpot>();
                spot.AddSpotFactory<SpotRouteTargetSpot>();

                }

            }
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
        _ = await manager.CreateAsync<SpotRouteTargetSpot>();
        await WaitForAcceptedRoutePeerAsync(nodeRuntime, "play");
        var route = await RetryAsync(
            () => resolver.ResolveSpotRemoteAddressAsync(nodeRuntime.Spots.Single().SpotRid, CancellationToken.None).AsTask(),
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
                var reply = await runtime.RequestToSpotViaRouterChannelAsync(
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
                    ZLinkMessageKind.Request,
                    route.RouterChannelId,
                    "UnknownSpotReq",
                    TimeSpan.FromMilliseconds(500));
                var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                    header,
                    new SpotRouteTargetRequest("missing"));
                var reply = await runtime.RequestToSpotViaRouterChannelAsync(
                        route.RouterChannelId,
                        route.TargetNodeRid,
                        route.SpotRid,
                        parts,
                        TimeSpan.FromMilliseconds(500),
                        CancellationToken.None)
                    .ConfigureAwait(false);
                try
                {
                    var error = Assert.Throws<InvalidOperationException>(() =>
                        ZLinkClientCallCodec.DecodeEnvelopeReply<SpotRouteTargetReply>(
                            reply,
                            "SPOT missing route reply is empty.",
                            "SPOT missing route request failed."));
                    return error.Message.Contains("UnknownSpotReq", StringComparison.Ordinal);
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

        var observed = await evidence.WaitAsync(TimeSpan.FromSeconds(2));
        Assert.Equal(ZLinkDispatchErrorSurface.SpotRoute, observed.Surface);
        Assert.Equal(ZLinkDispatchMessageKind.Request, observed.MessageKind);
        Assert.Equal(ZLinkDispatchErrorReason.HandlerMissing, observed.Reason);
        Assert.Equal(ZLinkDispatchErrorAction.ReplyError, observed.Action);
        Assert.Equal("UnknownSpotReq", observed.PacketName);
        Assert.Equal(spotChannel, observed.ChannelName);
        Assert.Equal(route.SpotRid.ToString(), observed.SpotRid);

        await WaitForDispatchLogFileAsync(logPath, TimeSpan.FromSeconds(2));
        var logText = await File.ReadAllTextAsync(logPath);
        Assert.Contains("dispatch-error", logText, StringComparison.Ordinal);
        Assert.Contains("surface=SpotRoute", logText, StringComparison.Ordinal);
        Assert.Contains("reason=HandlerMissing", logText, StringComparison.Ordinal);
        Assert.Contains("action=ReplyError", logText, StringComparison.Ordinal);
        Assert.Contains("packetName=UnknownSpotReq", logText, StringComparison.Ordinal);
        Assert.Contains($"spotRid={route.SpotRid}", logText, StringComparison.Ordinal);

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
                await runtime.SendToSpotViaRouterChannelAsync(
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

    private static async Task WaitForDispatchLogFileAsync(string path, TimeSpan timeout)
    {
        using var timeoutCts = new CancellationTokenSource(timeout);
        while (!timeoutCts.IsCancellationRequested)
        {
            if (File.Exists(path)
                && (await File.ReadAllTextAsync(path, timeoutCts.Token))
                    .Contains("dispatch-error", StringComparison.Ordinal))
            {
                return;
            }

            await Task.Delay(25, timeoutCts.Token);
        }

        throw new TimeoutException($"Timed out waiting for dispatch error log file '{path}'.");
    }
}
