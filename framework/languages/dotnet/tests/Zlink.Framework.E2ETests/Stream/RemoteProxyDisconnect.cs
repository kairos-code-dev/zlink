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
    public async Task BoundSessionDisconnect_FromRemoteActor_Closes_Client_Without_Session_Disconnect_Callback()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var sessionSpotEndpoint = GetFreeTcpEndpoint();
        var sessionSpotRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var playSpotRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.From($"remote-disconnect-session-{Guid.NewGuid():N}");
        var playRid = RoutingId.From($"remote-disconnect-play-{Guid.NewGuid():N}");
        var actorId = "remote-disconnect-player-1";
        var actorRecorder = new ActorDispatchRecorder();
        GatewaySessionRecorder? sessionRecorder = null;
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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.UseDiscovery(_ => { });
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind(playSpotRouterEndpoint);
                        router.SetRoutingId(playRid);
                        router.UseManualConnections(connections => connections.Connect(sessionSpotRouterEndpoint));
                    });
                });
                });
            });
        });
        var actorManager = playHost.Services.GetRequiredService<IZLinkActorManager>();
        var actor = await actorManager
            .GetOrCreateAsync(actorId, "player");
        var joined = await actor.Context.JoinEntrySpot(playRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .SubmitAsync();
        sessionRecorder = new GatewaySessionRecorder(actorId, joined);

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.UseDiscovery(_ => { });
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind(sessionSpotRouterEndpoint);
                        router.SetRoutingId(sessionRid);
                        router.UseManualConnections(connections => connections.Connect(playSpotRouterEndpoint));
                    });
                });
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AttachActorGateway("actor-node");
                    stream.RegisterSession<GatewayRelaySession>();
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
            Assert.True(
                bindReply.Header.Kind == ZlinkStreamMessageKind.Response,
                Encoding.UTF8.GetString(bindReply.Payload));
            Assert.Equal("play:bind-remote", bindBody?.Value);
            await RetryAsync(
                () => actorRecorder.CreatedCount == 1
                    && playHost.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                        .TryGetActorBoundSession(actorId, out _)
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
                        .TryGetActorBoundSession(actorId, out _)
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
