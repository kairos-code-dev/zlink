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
        var localRid = RoutingId.FromString("0404");
        var actorRecorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(routerEndpoint, services =>
        {
            services.AddSingleton(actorRecorder);
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<LocalNotifyDisconnectSession>();
            services.AddZLinkFramework(options =>
            {
                options.AddActorFactory<GatewayActorFactory>("player");
                options.AddSpotMesh("actor-node", mesh =>
                {
                    mesh.UseDiscovery(_ => { });
                    mesh.AddNode("actor-node", spot =>
                {
                    spot.Bind(spotEndpoint);
                });
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
                    stream.AttachActorGateway("actor-node");
                    stream.RegisterSession<LocalNotifyDisconnectSession>();
                });
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
