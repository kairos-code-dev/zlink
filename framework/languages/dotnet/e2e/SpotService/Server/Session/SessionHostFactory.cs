using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using SpotService.Server.Session.Handlers;
using SpotService.Server.Session.Spots;

namespace SpotService.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, defaultRole: "session");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
            framework.AddRouteMesh(SpotServiceNames.ControlChannel)
                .EnableServer(Require(options.ControlEndpoint, "--control-endpoint"))
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddHandlerGroup("play");
            framework.AddSpotMesh(SpotServiceNames.SpotChannel)
                .UseRegistrySpotResolver()
                .EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<ScenarioEntrySpot>()
                .AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType);
            framework.AddStreamNode(SpotServiceNames.StreamNode)
                .Bind(Require(options.StreamEndpoint, "--stream-endpoint"))
                .RegisterSession<ScenarioSession>();
            if (!string.IsNullOrWhiteSpace(options.TlsStreamEndpoint))
            {
                framework.AddStreamNode(SpotServiceNames.TlsStreamNode)
                    .Bind(options.TlsStreamEndpoint)
                    .SetTlsServer(
                        Require(options.TlsCertPath, "--tls-cert-path"),
                        Require(options.TlsKeyPath, "--tls-key-path"))
                    .RegisterSession<ScenarioSession>();
            }
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitRequest request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected =>
                    entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/channel/control-ping/{targetRid}", async (
            string targetRid,
            ControlPingReq request,
            IZLinkRouteClient route) =>
        {
            var reply = await route.Request(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(targetRid),
                    request)
                .PacketName("ControlPingReq")
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ControlPingReply>();
            return Results.Ok(reply);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapPost("/crash", () =>
        {
            ThreadPool.QueueUserWorkItem(_ =>
            {
                Thread.Sleep(50);
                System.Diagnostics.Process.GetCurrentProcess().Kill(entireProcessTree: false);
            });
            return Results.Accepted();
        });
        return app;
    }

static string Require(string? value, string optionName)
    => string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{optionName} is required.")
        : value;

}
