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
    public async Task ActorRefNotifyDisconnected_Notifies_Local_Bound_Actor()
    {
        var streamEndpoint = GetFreeTcpEndpoint();
        var routerEndpoint = GetFreeTcpEndpoint();
        var spotEndpoint = GetFreeTcpEndpoint();
        var localRid = RoutingId.FromString("0303");
        var recorder = new ActorDispatchRecorder();
        var sessionRecorder = new GatewaySessionRecorder();
        using var callbackCapture = CallbackExceptionCapture.Start();

        var host = await CreateHostAsync(routerEndpoint, services =>
        {
            services.AddSingleton(recorder);
            services.AddSingleton(sessionRecorder);
            services.AddScoped<GatewayActorFactory>();
            services.AddScoped<GatewayActorHandler>();
            services.AddScoped<GatewaySessionDisconnectHandler>();
            services.AddScoped<GatewaySessionDisconnectRequestHandler>();
            services.AddScoped<LocalNotifyDisconnectSession>();
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
                    stream.AddHeaderSession<LocalNotifyDisconnectSession>();
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
                () => recorder.DisconnectedCount > 0 && callbackCapture.IsEmpty,
                TimeSpan.FromSeconds(5));
            callbackCapture.ThrowIfAny();
        }
        finally
        {
            await host.StopAsync();
            host.Dispose();
        }
    }
}
