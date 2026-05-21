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

public sealed class RemoteSessionRelayTests : StreamTestSupport
{
    [Fact]
    public async Task SessionActorDispatch_Relays_Stream_Request_And_Routes_Request_To_Bound_Actor_By_Sequence()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString("0101");
        var playRid = RoutingId.FromString("0202");
        var proxyRecorder = new ActorDispatchRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(proxyRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddZLinkFramework(options =>
            {
                options.ConfigureMetadata(metadata =>
                {
                    metadata.ForwardApplicationKey("trace-id");
                });
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
            services.AddSingleton(new GatewaySessionRecorder());
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
                    new ZlinkStreamRequestSeq(101),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty
                        .With("trace-id", "trace-101")
                        .With("tenant-id", "tenant-denied")),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("from-client"), JsonOptions)));

            var relayReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(101));
            var relayBody = JsonSerializer.Deserialize<GatewayPong>(relayReply.Payload, JsonOptions);
            Assert.Equal(ZlinkStreamMessageKind.Response, relayReply.Header.Kind);
            Assert.Equal("play:from-client", relayBody?.Value);
            Assert.Equal(101UL, relayBody?.RequestSeq);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-101", proxyRecorder.LastTraceId);
            Assert.False(proxyRecorder.ForwardedTenantId);

            var sessionActor = (GatewayActor)(await playHost.Services
                    .GetRequiredService<IZLinkActorManager>()
                    .FindAsync("player-1")
                ?? throw new InvalidOperationException("Actor was not created."));
            var sessionProxy = sessionActor.Context.SessionProxy;
            var clientReplyTask = Task.Run(() =>
            {
                var request = ReceiveFrame(network);
                Assert.Equal(ZlinkStreamMessageKind.Request, request.Header.Kind);
                Assert.Equal("client.echo", request.Header.Name);
                Assert.NotNull(request.Header.RequestSeq);
                var ping = JsonSerializer.Deserialize<GatewayPing>(request.Payload, JsonOptions);
                Assert.Equal("from-play", ping?.Value);
                SendAll(network, BuildStreamPacketFrame(
                    new ZlinkStreamHeader(
                        ZlinkStreamMessageKind.Response,
                        ZlinkStreamCodec.Json,
                        ZlinkStreamHeaderFlags.HasRequestSeq,
                        request.Header.RequestSeq,
                        request.Header.Name,
                        ZlinkStreamMetadata.Empty),
                    JsonSerializer.SerializeToUtf8Bytes(
                        new GatewayPong("client:from-play", request.Header.RequestSeq!.Value.Value),
                        JsonOptions)));
            });
            var gatewayReply = await sessionProxy.Request(new GatewayPing("from-play"))
                .PacketName("client.echo")
                .Timeout(TimeSpan.FromSeconds(10))
                .SubmitAsync<GatewayPong>();

            await clientReplyTask;
            Assert.Equal("client:from-play", gatewayReply.Value);
            Assert.NotEqual(0UL, gatewayReply.RequestSeq);

            await sessionProxy.Send(new GatewayPing("one-way-from-play"))
                .PacketName("client.notify")
                .Submit();
            var clientPush = ReceiveFrame(network);
            Assert.Equal(ZlinkStreamMessageKind.Send, clientPush.Header.Kind);
            Assert.Equal("client.notify", clientPush.Header.Name);
            var clientPushBody = JsonSerializer.Deserialize<GatewayPing>(clientPush.Payload, JsonOptions);
            Assert.Equal("one-way-from-play", clientPushBody?.Value);

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(102),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty
                        .With("trace-id", "trace-session-relay")),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("from-session-relay"), JsonOptions)));
            var secondRelayReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(102));
            var secondRelayBody = JsonSerializer.Deserialize<GatewayPong>(secondRelayReply.Payload, JsonOptions);

            Assert.Equal("play:from-session-relay", secondRelayBody?.Value);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-session-relay", proxyRecorder.LastTraceId);
            callbackCapture.ThrowIfAny();

            client.Dispose();
            await RetryAsync(
                () => proxyRecorder.DisconnectedCount > 0 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }
}
