using Microsoft.AspNetCore.Mvc;
using RuntimeMonitoring.Server.Service.Handlers;
using RuntimeMonitoring.Server.Service.Support;
using RuntimeMonitoring.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;

namespace RuntimeMonitoring.Server.Service;

internal static class ServiceHostFactory
{
    public static WebApplication CreateAll(string[] args)
    {
        return Create(args, ServiceMonitorProfile.All);
    }

    public static WebApplication CreateSocketFilter(string[] args)
    {
        return Create(args, ServiceMonitorProfile.SocketFilter);
    }

    public static WebApplication CreateThrowing(string[] args)
    {
        return Create(args, ServiceMonitorProfile.Throwing);
    }

    public static WebApplication Create(string[] args, ServiceMonitorProfile profile)
    {
        var options = ServerOptions.Parse(args, "service");
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.EvidenceFile));
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventRecorder>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSpotEvent>, SpotEventRecorder>();
        if (profile == ServiceMonitorProfile.Throwing)
            builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, ThrowingSocketEventRecorder>();

        builder.Services.AddZLinkFramework(framework =>
        {
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            var channel = framework.AddClientServerChannel(RuntimeMonitoringNames.Channel)
                .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            channel.AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");

            if (profile == ServiceMonitorProfile.All)
            {
                var spotMesh = framework.AddSpotMesh(RuntimeMonitoringNames.SpotChannel);
                spotMesh.EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
                    .SetRoutingId(RoutingId.From(options.Rid))
                    .EnablePubSub(Require(options.SpotPubEndpoint, "--spot-pub-endpoint"))
                    .AddEntrySpot<MonitoringEntrySpot>();
            }
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            if (profile == ServiceMonitorProfile.SocketFilter)
                monitor.AddSocketEvents(
                    RuntimeMonitoringNames.ChannelServerSource,
                    ZLinkSocketEventKind.ConnectionReady);
            else
                monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelServerSource);

            if (profile == ServiceMonitorProfile.All)
                monitor.AddSpotEvents(RuntimeMonitoringNames.SpotNode, TimeSpan.FromMilliseconds(100));
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
                               entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
                           && request.ContainsAnyGroups.All(group =>
                               group.Any(expected =>
                                   entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))),
                timeout,
                cancellationToken);
            return Results.Ok(snapshot);
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
        });
        app.MapPost("/admin/drain", (
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.ClientServerChannel(RuntimeMonitoringNames.Channel).ConfigureServerSocket().Weight = 0;
            evidence.Add($"admin|rid={evidence.Rid}|action=drain|weight=0");
            return Results.Ok(new { status = "drained", weight = 0 });
        });
        app.MapPost("/admin/restore", (
            [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
            [FromServices] EvidenceStore evidence) =>
        {
            runtimeOptions.ClientServerChannel(RuntimeMonitoringNames.Channel).ConfigureServerSocket().Weight = 100;
            evidence.Add($"admin|rid={evidence.Rid}|action=restore|weight=100");
            return Results.Ok(new { status = "restored", weight = 100 });
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
