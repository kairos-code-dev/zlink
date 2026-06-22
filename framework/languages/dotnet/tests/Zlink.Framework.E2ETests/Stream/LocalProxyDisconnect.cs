using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework.AspNetCore;

namespace Zlink.Framework.E2ETests;

public sealed class LocalProxyDisconnectTests : StreamTestSupport
{
    [Fact]
    public async Task BoundSessionDisconnect_FromLocalActor_Closes_Client_Without_Session_Disconnect_Callback()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var routerEndpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.From("0404");
        var actorRecorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(routerEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewayEntrySpot>();
            services.AddScoped<GatewayEntrySpotActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddScoped<LocalNotifyDisconnectSession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                {
                    var mesh = options.AddSpotMesh("actor-node");
                    {
                        var spot = mesh.AddNode("actor-node");
                    {
                        var router = spot.EnableRouter(spotEndpoint);
                        router.SetRouterRoutingId(RoutingId.From("local-notify-actor-node"));

                    }
                    spot.AddEntrySpot<GatewayEntrySpot>();

                    }

                }
                {
                    var routed = options.AddRouteMeshChannel("gateway");
                    routed.EnableServer(routerEndpoint);
                    routed.SetRoutingId(localRid);
                    routed.EnableClient(routerEndpoint);

                }
                {
                    var stream = options.AddStreamNode("client.stream");
                    stream.Bind(streamEndpoint);
                    stream.AttachActorGateway("actor-node");
                    stream.RegisterSession<LocalNotifyDisconnectSession>();

                }
            });
        });

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();
            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "open",
                    ZlinkStreamMetadata.Empty),
                "\"open\""u8));
            await RetryAsync(
                () => actorRecorder.CreatedCount == 1
                    && host.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                        .TryGetActorBoundSession("local-player-1", out _)
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Send,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.None,
                    null,
                    "session.disconnect",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("close-local"), JsonOptions)));

            await RetryAsync(
                () => actorRecorder.ProxyDisconnectCount == 1
                    && !host.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                        .TryGetActorBoundSession("local-player-1", out _)
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, sessionRecorder.DisconnectedCount);
            Assert.Equal(0, actorRecorder.DisconnectedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }
}
