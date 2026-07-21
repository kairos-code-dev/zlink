using Microsoft.Extensions.Configuration;

using System.Diagnostics;
using SpotService.Server.Session.Handlers;
using SpotService.Server.Session.Spots;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;

using Zlink.Framework.Locations.Redis;

namespace SpotService.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ServerOptions.Parse(args, "session");
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
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));

        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
                // Crash-recovery scenarios re-claim actors from a killed
                // node; a short owner lease keeps that takeover window
                // within the scenario's patience.
                var locations = framework.ConfigureLocations();
                locations.HeartbeatInterval = TimeSpan.FromSeconds(1);
                locations.OwnerLeaseTtl = TimeSpan.FromSeconds(10);
                locations.PollingInterval = TimeSpan.FromMilliseconds(500);
            }
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            var controlMesh = framework.AddRouteMesh(SpotServiceNames.ControlChannel)
                .Listen(Require(options.ControlEndpoint, "ControlEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            controlMesh.ChannelName(SpotServiceNames.ControlChannel);
            controlMesh
                .AddRouteRequestHandler<EnsureActorHandler>()
                .AddRouteRequestHandler<ControlPingHandler>()
                .AddRouteRequestHandler<CreateSpotHandler>()
                .AddRouteRequestHandler<CloseSpotHandler>()
                .AddRouteRequestHandler<SpotTypeMismatchHandler>();
            var mesh22 = framework.AddRouteMesh(SpotServiceNames.SpotChannel)
                                .Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<ScenarioEntrySpot>()
                .AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType);
            mesh22.ChannelName(SpotServiceNames.SpotChannel);
            framework.AddStreamNode(SpotServiceNames.StreamNode)
                .Bind(Require(options.StreamEndpoint, "StreamEndpoint"))
                .EnableActorDispatch(SpotServiceNames.SpotChannel)
                .AddSession<ScenarioSession>();
            if (!string.IsNullOrWhiteSpace(options.TlsStreamEndpoint))
                framework.AddStreamNode(SpotServiceNames.TlsStreamNode)
                    .Bind(options.TlsStreamEndpoint)
                    .EnableActorDispatch(SpotServiceNames.SpotChannel)
                    .SetTlsServer(
                        Require(options.TlsCertPath, "TlsCertPath"),
                        Require(options.TlsKeyPath, "TlsKeyPath"))
                    .AddSession<ScenarioSession>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
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
            var reply = await route.RequestToNode(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(targetRid),
                    request)
                .Timeout(TimeSpan.FromSeconds(5))
                .Async<ControlPingRes>();
            return Results.Ok(reply);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        return app;
    }

    private static string Require(string? value, string optionName)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{optionName} is required.")
            : value;
    }
}
