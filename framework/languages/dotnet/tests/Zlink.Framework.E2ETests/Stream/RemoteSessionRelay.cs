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
        var sessionSpotEndpoint = GetFreeTcpEndpoint();
        var sessionSpotRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var playSpotRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.Of($"remote-relay-session-{Guid.NewGuid():N}");
        var playRid = RoutingId.Of($"remote-relay-play-{Guid.NewGuid():N}");
        var actorId = "remote-relay-player-1";
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
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.UseDiscovery(_ => { });
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.Bind(playSpotEndpoint);
                    spot.EnableRouter(router =>
                    {
                        router.Bind(playSpotRouterEndpoint);
                        router.ConfigureRouting(routing => routing.RoutingId = playRid);
                        router.UseManualConnections(connections => connections.Connect(sessionSpotRouterEndpoint));
                    });
                });
                });
            });
        });
        var actorManager = playHost.Services.GetRequiredService<IZLinkActorManager>();
        await actorManager
            .GetOrCreateAsync(actorId, "player");
        var remoteAddress = await actorManager.GetRemoteAddressAsync(actorId, "player");

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new GatewaySessionRecorder(actorId, remoteAddress));
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.UseDiscovery(_ => { });
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.Bind(sessionSpotEndpoint);
                    spot.EnableRouter(router =>
                    {
                        router.Bind(sessionSpotRouterEndpoint);
                        router.ConfigureRouting(routing => routing.RoutingId = sessionRid);
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
                    new ZlinkStreamRequestSeq(101),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty
                        .With("trace-id", "trace-101")
                        .With("tenant-id", "tenant-denied")),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("from-client"), JsonOptions)));

            var relayReply = ReceiveFrame(network, new ZlinkStreamRequestSeq(101));
            var relayBody = JsonSerializer.Deserialize<GatewayPong>(relayReply.Payload, JsonOptions);
            Assert.True(
                relayReply.Header.Kind == ZlinkStreamMessageKind.Response,
                Encoding.UTF8.GetString(relayReply.Payload));
            Assert.Equal("play:from-client", relayBody?.Value);
            Assert.Equal(101UL, relayBody?.RequestSeq);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-101", proxyRecorder.LastTraceId);
            Assert.False(proxyRecorder.ForwardedTenantId);

            var sessionActor = (GatewayActor)(await playHost.Services
                    .GetRequiredService<IZLinkActorManager>()
                    .FindAsync(actorId)
                ?? throw new InvalidOperationException("Actor was not created."));
            var boundSession = sessionActor.Context.BoundSession;
            await boundSession.Send(new GatewayPing("one-way-from-play"))
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
                () => !playHost.Services.GetRequiredService<ZLinkFrameworkRuntime>()
                        .TryGetActorBoundSession(actorId, out _)
                    && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, proxyRecorder.DisconnectedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }
}
