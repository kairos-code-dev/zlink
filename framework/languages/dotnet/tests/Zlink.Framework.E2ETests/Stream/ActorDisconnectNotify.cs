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

public sealed class ActorDisconnectNotifyTests : StreamTestSupport
{
    [Fact]
    public async Task ClientClose_Cleans_Session_Without_Actor_Disconnect_Callback()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var recorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
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
            services.AddScoped<LocalNotifyDisconnectSession>();
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
                        router.SetRouterBind(spotEndpoint);
                        router.SetRoutingId(RoutingId.From("local-notify-actor-node"));
                    });
                    spot.AddEntrySpot<GatewayEntrySpot>();
                });
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
                () => recorder.CreatedCount == 1 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();

            client.Dispose();
            await RetryAsync(
                () => sessionRecorder.DisconnectedCount > 0 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            Assert.Equal(0, recorder.DisconnectedCount);
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }
}
