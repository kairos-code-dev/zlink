using Microsoft.Extensions.Configuration;

using Google.Protobuf.WellKnownTypes;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using RegistrationCodec.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Codecs.MessagePack;
using Zlink.Framework.Codecs.Protobuf;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.E2E.Diagnostics;

namespace RegistrationCodec.Server.CodecRequester;

internal static class CodecRequesterHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = CodecRequesterOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton(new E2eMessageFlowListener(
            Path.Combine(options.LogDir, $"{options.Rid}-flow.log"),
            options.Rid));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch().Diagnostics
                .SetLevel(ZLinkDiagnosticsLevel.Normal);
            framework.Codecs.Use(ZLinkProtobufCodec.Default);
            framework.Codecs.Use(ZLinkMessagePackCodec.Default);
            var mesh = framework.AddRouteMesh(RegistrationCodecNames.Channel)
                .Listen("tcp://127.0.0.1:0")
                .SetRoutingId(RoutingId.From(options.Rid));
            mesh.Channel(RegistrationCodecNames.Channel).Client();
            mesh.PeerConnections.Connect(options.ChannelEndpoint);
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapGet("/topology/ready", (IZLinkRouteMeshRuntime runtime) =>
        {
            var snapshot = runtime.GetStatus(RegistrationCodecNames.Channel);
            return Results.Ok(new
            {
                ready = snapshot.Peers.Any(static peer =>
                            peer.State == ZLinkPeerState.Ready)
                        && snapshot.Channels.Any(static channel =>
                            channel.ChannelName == RegistrationCodecNames.Channel
                            && channel.IsReady)
            });
        });
        app.MapPost("/codec/protobuf/request", async (
            IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            try
            {
                var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel,
                        new StringValue { Value = "rc-b5" })
                    .Timeout(TimeSpan.FromSeconds(2))
                    .Async<StringValue>(cancellationToken);
                return Results.Ok(new CodecMismatchProbeRes(false, null, reply.Value));
            }
            catch (Exception ex)
            {
                return Results.Ok(new CodecMismatchProbeRes(true, ex.GetType().Name, null));
            }
        });
        app.MapPost("/codec/json/request", async (
            IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            var reply = await channel.RequestToChannel(RegistrationCodecNames.Channel,
                    new JsonEchoReq("rc-b5-json"))
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<EchoRes>(cancellationToken);
            return Results.Ok(reply);
        });
        return app;
    }
}
