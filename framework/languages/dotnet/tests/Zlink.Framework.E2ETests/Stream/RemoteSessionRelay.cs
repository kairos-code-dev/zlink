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
        var registryPubEndpoint = GetFreeTcpEndpoint();
        var registryRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var sessionSpotEndpoint = GetFreeTcpEndpoint();
        var sessionSpotRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var playSpotRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.From($"remote-relay-session-{Guid.NewGuid():N}");
        var playRid = RoutingId.From($"remote-relay-play-{Guid.NewGuid():N}");
        var actorId = "remote-relay-player-1";
        var proxyRecorder = new ActorDispatchRecorder();
        GatewaySessionRecorder? sessionRecorder = null;
        using var callbackCapture = CallbackExceptionCapture.Start();

        var registryBuilder = Host.CreateApplicationBuilder();
        registryBuilder.Services.AddZLinkRegistry(options =>
        {
            options.PubEndpoint = registryPubEndpoint;
            options.RouterEndpoint = registryRouterEndpoint;
        });
        using var registryHost = registryBuilder.Build();
        await registryHost.StartAsync();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(proxyRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewayEntrySpot>();
            services.AddScoped<GatewayEntrySpotActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddZLinkFramework(options =>
            {
                options.ConfigureMetadata(metadata =>
                {
                    metadata.Forward("trace-id");
                });
                options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind(playSpotRouterEndpoint);
                        router.SetRoutingId(playRid);
                    });
                    spot.AddEntrySpot<GatewayEntrySpot>();
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

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            sessionRecorder = new GatewaySessionRecorder(actorId, joined);
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.UseDiscovery(discovery => discovery.Add(registryRouterEndpoint));
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.SetRouterBind(sessionSpotRouterEndpoint);
                        router.SetRoutingId(sessionRid);
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
            var playNodeRuntime = playHost.Services
                .GetRequiredService<ZLinkFrameworkRuntime>()
                .GetSpotNodeRuntime("actor-node");
            var sessionNodeRuntime = sessionHost.Services
                .GetRequiredService<ZLinkFrameworkRuntime>()
                .GetSpotNodeRuntime("actor-node");
            await WaitForActorNodeRouterPeersAsync(playNodeRuntime, sessionNodeRuntime);

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
            var relayPayload = (relayReply.Header.Flags & ZlinkStreamHeaderFlags.PayloadCompressed) != 0
                ? ZLinkStreamProtocolDefaults.Lz4Decompress(relayReply.Payload).ToArray()
                : relayReply.Payload;
            var relayBody = JsonSerializer.Deserialize<GatewayPong>(relayPayload, JsonOptions);
            Assert.True(
                relayReply.Header.Kind == ZlinkStreamMessageKind.Response,
                Encoding.UTF8.GetString(relayReply.Payload));
            Assert.True((relayReply.Header.Flags & ZlinkStreamHeaderFlags.PayloadCompressed) != 0);
            Assert.True(relayReply.Header.Metadata.TryGet("reply-trace-id", out var replyTraceId));
            Assert.Equal("reply:trace-101", replyTraceId);
            Assert.Equal("play:from-client", relayBody?.Value);
            Assert.Equal(101UL, relayBody?.RequestSeq);
            Assert.Equal("relay.echo", proxyRecorder.LastPacketName);
            Assert.Equal("trace-101", proxyRecorder.LastTraceId);
            Assert.False(proxyRecorder.ForwardedTenantId);
            Assert.True(sessionRecorder?.PostRelayPayloadLength > 0);

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
            var secondRelayPayload = (secondRelayReply.Header.Flags & ZlinkStreamHeaderFlags.PayloadCompressed) != 0
                ? ZLinkStreamProtocolDefaults.Lz4Decompress(secondRelayReply.Payload).ToArray()
                : secondRelayReply.Payload;
            var secondRelayBody = JsonSerializer.Deserialize<GatewayPong>(secondRelayPayload, JsonOptions);

            Assert.Equal("play:from-session-relay", secondRelayBody?.Value);
            Assert.True(secondRelayReply.Header.Metadata.TryGet("reply-trace-id", out var secondReplyTraceId));
            Assert.Equal("reply:trace-session-relay", secondReplyTraceId);
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
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost, registryHost);
        }
    }

    private static async Task WaitForActorNodeRouterPeersAsync(
        ZLinkSpotNodeRuntime playNodeRuntime,
        ZLinkSpotNodeRuntime sessionNodeRuntime)
    {
        var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(5);
        while (DateTime.UtcNow < deadline)
        {
            if (HasActorNodeRouterPeer(playNodeRuntime)
                && HasActorNodeRouterPeer(sessionNodeRuntime))
            {
                return;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(150));
        }

        throw new TimeoutException(
            "Actor node router peers were not discovered. "
            + $"play=[{DescribeActorNodePeers(playNodeRuntime)}], "
            + $"session=[{DescribeActorNodePeers(sessionNodeRuntime)}]");
    }

    private static bool HasActorNodeRouterPeer(ZLinkSpotNodeRuntime nodeRuntime)
    {
        return nodeRuntime.Node.Peers().Any(
            peer => string.Equals(peer.ChannelName, "actor-node", StringComparison.Ordinal)
                && peer.Kind == ZLinkSpotPeerKind.RouterChannel);
    }

    private static string DescribeActorNodePeers(ZLinkSpotNodeRuntime nodeRuntime)
    {
        var peers = nodeRuntime.Node.Peers()
            .Select(static peer =>
                $"{peer.ChannelName}:{peer.PeerEndpoint}:{peer.Source}:{peer.Kind}:{peer.State}");
        return string.Join(", ", peers);
    }
}
