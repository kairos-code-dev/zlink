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

public sealed class LocalSessionRelayTests : StreamTestSupport
{
    [Fact]
    public async Task LocalSessionActorDispatch_Relays_Stream_Request_And_Replies_From_Request_Handler()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var spotRouterEndpoint = GetFreeTcpEndpoint();
        var actorNodeRid = RoutingId.From($"local-relay-node-{Guid.NewGuid():N}");
        var actorId = "local-relay-player-1";
        var recorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder(actorId);
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(spotEndpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewayEntrySpot>();
            services.AddScoped<GatewayEntrySpotActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(spotRouterEndpoint);
                        router.SetRoutingId(actorNodeRid);
                    });
                    spot.AddEntrySpot<GatewayEntrySpot>();
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

        var actor = await host.Services.GetRequiredService<IZLinkActorManager>()
            .GetOrCreateAsync(actorId, "player");
        var actorRef = await actor.Context.JoinEntrySpot(actorNodeRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        sessionRecorder.SetActor(actorRef);

        try
        {
            using var client = ConnectRawClient(streamEndpoint);
            var network = client.GetStream();

            SendAll(network, BuildStreamPacketFrame(
                new ZlinkStreamHeader(
                    ZlinkStreamMessageKind.Request,
                    ZlinkStreamCodec.Json,
                    ZlinkStreamHeaderFlags.HasRequestSeq,
                    new ZlinkStreamRequestSeq(401),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("local"), JsonOptions)));

            var reply = ReceiveFrame(network, new ZlinkStreamRequestSeq(401));

            Assert.True(
                reply.Header.Kind == ZlinkStreamMessageKind.Response,
                Encoding.UTF8.GetString(reply.Payload));
            var payload = (reply.Header.Flags & ZlinkStreamHeaderFlags.PayloadCompressed) != 0
                ? ZLinkStreamProtocolDefaults.Lz4Decompress(reply.Payload).ToArray()
                : reply.Payload;
            var body = JsonSerializer.Deserialize<GatewayPong>(payload, JsonOptions);
            Assert.Equal("play:local", body?.Value);
            Assert.Equal(101UL, body?.RequestSeq);
            Assert.Equal("relay.echo", recorder.LastPacketName);
            Assert.True(sessionRecorder.PostRelayPayloadLength > 0);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await host.StopAsync();
        }
    }

    [Fact]
    public async Task RemoteActorDispatch_DoesNot_Create_MissingActor()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var sessionRouterEndpoint = GetFreeTcpEndpoint();
        var sessionSpotEndpoint = GetFreeTcpEndpoint();
        var sessionSpotRouterEndpoint = GetFreeTcpEndpoint();
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var playSpotRouterEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.From($"missing-remote-session-{Guid.NewGuid():N}");
        var playRid = RoutingId.From($"missing-remote-play-{Guid.NewGuid():N}");
        var actorId = "missing-remote-player-1";
        var actorRecorder = new ActorDispatchRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewayEntrySpot>();
            services.AddScoped<GatewayEntrySpotActorHandler>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(playSpotRouterEndpoint);
                        router.SetRoutingId(playRid);
                        router.UseManualConnections(connections => connections.Connect(sessionSpotRouterEndpoint));
                    });
                    spot.AddEntrySpot<GatewayEntrySpot>();
                });
                });
            });
        });
        var missingActor = await playHost.Services.GetRequiredService<IZLinkActorManager>()
            .GetOrCreateAsync(actorId, "player");
        await missingActor.Context.JoinEntrySpot(playRid)
            .Timeout(TimeSpan.FromSeconds(5))
            .Async();
        var createdBeforeDispatch = actorRecorder.CreatedCount;

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new GatewaySessionRecorder(actorId));
            services.AddScoped<MissingRemoteActorRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.EnableRouter(router =>
                    {
                        router.BindRouter(sessionSpotRouterEndpoint);
                        router.SetRoutingId(sessionRid);
                        router.UseManualConnections(connections => connections.Connect(playSpotRouterEndpoint));
                    });
                });
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AttachActorGateway("actor-node");
                    stream.RegisterSession<MissingRemoteActorRelaySession>();
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
                    new ZlinkStreamRequestSeq(301),
                    "relay.echo",
                    ZlinkStreamMetadata.Empty),
                JsonSerializer.SerializeToUtf8Bytes(new GatewayPing("missing-remote"), JsonOptions)));

            var error = ReceiveFrame(network, new ZlinkStreamRequestSeq(301));
            Assert.Equal(ZlinkStreamMessageKind.Error, error.Header.Kind);
            Assert.Equal(createdBeforeDispatch, actorRecorder.CreatedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }
}
