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

public sealed class RemoteProxyDisconnectTests : StreamTestSupport
{
    [Fact]
    public async Task SessionProxyDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString("0505");
        var playRid = RoutingId.FromString("0606");
        var actorRecorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(playSpotEndpoint);
                });
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(playRouterEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = playRid);
                    routed.UseManualConnections(connections => connections.Connect(sessionRouterEndpoint));
                });
            });
        });
        await playHost.Services.GetRequiredService<IZLinkActorManager>()
            .GetOrCreateAsync("player-1", "player");

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new TestActorRouteSnapshot(new ZLinkActorRoute("gateway", playRid, 1)));
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(sessionRouterEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = sessionRid);
                    routed.UseManualConnections(connections => connections.Connect(playRouterEndpoint));
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AddHeaderSession<GatewayRelaySession>();
                });
            });
        });

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(201),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("bind-remote"), JsonOptions)));
            var bindReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(201));
            var bindBody = JsonSerializer.Deserialize<GatewayPong>(bindReply.Payload, JsonOptions);
            Assert.Equal("play:bind-remote", bindBody?.Value);
            await RetryAsync(
                () => actorRecorder.CreatedCount == 1
                    && playHost.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                        .TryGetActorBoundSession("player-1", out _)
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(202),
                    "session.disconnect",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("close-remote"), JsonOptions)));
            var disconnectReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(202));
            var disconnectBody = JsonSerializer.Deserialize<GatewayPong>(disconnectReply.Payload, JsonOptions);
            Assert.Equal("disconnect:close-remote", disconnectBody?.Value);

            await RetryAsync(
                () => actorRecorder.ProxyDisconnectCount == 1
                    && !playHost.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                        .TryGetActorBoundSession("player-1", out _)
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, sessionRecorder.DisconnectedCount);
            Assert.Equal(0, actorRecorder.DisconnectedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }
}
