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
        var routerEndpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("0909");
        var recorder = new ActorDispatchRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(routerEndpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddSingleton(new GatewaySessionRecorder());
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddScoped<GatewayRelaySession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
                options.AddRouteMeshChannel("gateway", routed =>
                {
                    routed.Bind(routerEndpoint);
                    routed.ConfigureRouting(routing => routing.RoutingId = localRid);
                    routed.UseManualConnections(connections => connections.Connect(routerEndpoint));
                });
                options.AddStreamNode("client.stream", stream =>
                {
                    stream.Bind(streamEndpoint);
                    stream.AddHeaderSession<GatewayRelaySession>();
                });
            });
        });

        await host.Services.GetRequiredService<IZLinkActorManager>()
            .GetOrCreateAsync("player-1", "player");

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
            var body = JsonSerializer.Deserialize<GatewayPong>(reply.Payload, JsonOptions);

            Assert.True(
                reply.Header.Kind == ZlinkStreamMessageKind.Response,
                Encoding.UTF8.GetString(reply.Payload));
            Assert.Equal("play:local", body?.Value);
            Assert.Equal(101UL, body?.RequestSeq);
            Assert.Equal("relay.echo", recorder.LastPacketName);
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
        var playRouterEndpoint = GetFreeTcpEndpoint();
        var playSpotEndpoint = GetFreeTcpEndpoint();
        var sessionRid = RoutingId.FromString("0707");
        var playRid = RoutingId.FromString("0808");
        var actorRecorder = new ActorDispatchRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var playHost = await CreateHostAsync(playRouterEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
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

        var sessionHost = await CreateHostAsync(sessionRouterEndpoint, services =>
        {
            services.AddSingleton(new TestActorRouteSnapshot(new ZLinkActorRoute("gateway", playRid, 1)));
            services.AddSingleton(new GatewaySessionRecorder());
            services.AddScoped<MissingRemoteActorRelaySession>();
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
                    stream.AddHeaderSession<MissingRemoteActorRelaySession>();
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
            Assert.Equal(0, actorRecorder.CreatedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await ChannelMessagingTestSupport.StopHostsAsync(sessionHost, playHost);
        }
    }
}
