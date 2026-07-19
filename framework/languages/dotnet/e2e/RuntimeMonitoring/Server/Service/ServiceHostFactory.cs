using Microsoft.Extensions.Configuration;

using Microsoft.AspNetCore.Mvc;
using RuntimeMonitoring.Server.Service.Handlers;
using RuntimeMonitoring.Server.Service.Support;
using RuntimeMonitoring.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Spots;

using Zlink.Framework.Locations.Redis;

namespace RuntimeMonitoring.Server.Service;

internal static class ServiceHostFactory
{
    public static WebApplication CreateAll(string[] args)
    {
        var options = ServerOptions.Parse(args, "service");
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
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile, options.Rid));
        builder.Services.AddSingleton<LocationTopologyTransitionTracker>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventRecorder>();
        builder.Services.AddScoped<
            IZLinkRuntimeEventHandler<Zlink.Framework.Contracts.Configuration.ZLinkMeshRuntimeEvent>,
            MeshEventRecorder>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSpotEvent>, SpotEventRecorder>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>, LocationRuntimeEventRecorder>();

        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(options.RedisEndpoint)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            var channelMesh = framework.AddRouteMesh(RuntimeMonitoringNames.Channel)
                .Listen(Require(options.ChannelEndpoint, "ChannelEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            channelMesh.ChannelName(RuntimeMonitoringNames.Channel)
                .AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");

            var spotMesh = framework.AddRouteMesh(RuntimeMonitoringNames.SpotChannel);
            spotMesh.ChannelName(RuntimeMonitoringNames.SpotChannel);
            spotMesh.Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<MonitoringEntrySpot>()
                .AddSpotFactory<MonitoringSubjectSpot>();
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            // The channel is a RouteMesh in 10.0.0: wire-level identity comes
            // from the mesh runtime event stream (spec 50), not socket sources.
            monitor.AddMeshNodeEvents(RuntimeMonitoringNames.Channel);
            monitor.AddSpotEvents(RuntimeMonitoringNames.SpotNode, TimeSpan.FromMilliseconds(100));

            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
                monitor.AddLocationRuntimeEvents(
                    RuntimeMonitoringNames.LocationRuntimeSource,
                    TimeSpan.FromMilliseconds(100));
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
                               entries.Skip(request.AfterIndex)
                                   .Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
                           && request.ContainsAnyGroups.All(group =>
                               group.Any(expected =>
                                   entries.Skip(request.AfterIndex)
                                       .Any(entry => entry.Contains(expected, StringComparison.Ordinal)))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot.Skip(request.AfterIndex).ToArray());
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapPost("/profile/request", async (
            ProfileReq request,
            [FromServices] IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            var response = await channel.RequestToChannel(RuntimeMonitoringNames.Channel, RuntimeMonitoringNames.Channel, request)
                .Timeout(TimeSpan.FromSeconds(10))
                .Async<ProfileRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapPost("/admin/subject/create", async (
            [FromServices] IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            await spots.GetOrCreateAsync<MonitoringSubjectSpot>(
                RoutingId.From("monitor-subject"),
                cancellationToken);
            return Results.Ok(new { status = "created" });
        });
        app.MapPost("/admin/subject/close", async (
            [FromServices] IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var closed = await spots.CloseAsync(RoutingId.From("monitor-subject"), cancellationToken);
            return Results.Ok(new { status = closed ? "closed" : "not-found" });
        });
        app.MapPost("/admin/weight/exclude", (
            [FromServices] IZLinkRouteMeshRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.Channel(RuntimeMonitoringNames.Channel, RuntimeMonitoringNames.Channel).Weight = 0;
            evidence.Add($"admin|rid={evidence.Rid}|action=drain|weight=0");
            return Results.Ok(new { status = "drained", weight = 0 });
        });
        app.MapPost("/admin/weight/include", (
            [FromServices] IZLinkRouteMeshRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.Channel(RuntimeMonitoringNames.Channel, RuntimeMonitoringNames.Channel).Weight = 100;
            evidence.Add($"admin|rid={evidence.Rid}|action=restore|weight=100");
            return Results.Ok(new { status = "restored", weight = 100 });
        });
        app.MapPost("/admin/graceful-drain", async (
            [FromServices] IZLinkDrainControl drain,
            CancellationToken cancellationToken) =>
        {
            var result = await drain.DrainAsync(TimeSpan.FromSeconds(30), cancellationToken);
            return Results.Ok(result switch
            {
                Drained => new DrainResultRes(nameof(Drained)),
                ForceStopped forced => new DrainResultRes(nameof(ForceStopped), forced.Reason.ToString()),
                _ => throw new InvalidOperationException($"Unknown drain result '{result.GetType().Name}'.")
            });
        });
        return app;
    }

    private static string Require(string? value, string name)
    {
        return string.IsNullOrWhiteSpace(value)
            ? throw new InvalidOperationException($"{name} is required.")
            : value;
    }
}
