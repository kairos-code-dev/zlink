using Microsoft.Extensions.Configuration;
using StackExchange.Redis;

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
        builder.Services.AddSingleton<ApplicationDispatchGate>();
        builder.Services.AddSingleton<ObserverIsolationProbe>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventRecorder>();
        builder.Services.AddScoped<
            IZLinkRuntimeEventHandler<Zlink.Framework.Contracts.Configuration.ZLinkMeshRuntimeEvent>,
            MeshEventRecorder>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSpotEvent>, SpotEventRecorder>();
        builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>, LocationRuntimeEventRecorder>();

        builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
            {
                var redisConfiguration = ConfigurationOptions.Parse(options.RedisEndpoint);
                // This local failure-recovery fixture must surface a paused
                // store inside the three-second readiness boundary. The
                // production default command timeout would defer the first
                // failure observation for about five seconds.
                redisConfiguration.AsyncTimeout = 500;
                redisConfiguration.ConnectTimeout = 500;
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConfiguration(redisConfiguration)
                    .SetKeyPrefix(options.RedisKeyPrefix
                                  ?? throw new InvalidOperationException("Shared.RedisKeyPrefix is required."))));
            }
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);

            var channelMesh = framework.AddRouteMesh(RuntimeMonitoringNames.Channel)
                .Listen(Require(options.ChannelEndpoint, "ChannelEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid));
            channelMesh.ChannelName(RuntimeMonitoringNames.Channel)
                .AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");

            var spotMesh = framework.AddRouteMesh(RuntimeMonitoringNames.SpotChannel);
            spotMesh.ChannelName(RuntimeMonitoringNames.SpotChannel)
                .AddRequestHandler<
                    ProfileRequestHandler,
                    ProfileReq,
                    ProfileRes>("ProfileReq");
            spotMesh.Listen(Require(options.SpotRouterEndpoint, "SpotRouterEndpoint"))
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<MonitoringEntrySpot>()
                .AddSpotFactory<MonitoringSubjectSpot>();
            var spotRouter = spotMesh.ConfigureRouterSocket();
            spotRouter.SendHighWaterMark = 1;
            spotRouter.SendTimeout = TimeSpan.FromMilliseconds(250);
            spotRouter.MailboxMessageBudget = 1;
            spotRouter.MailboxByteBudget = 2 * 1024 * 1024;
        });
        builder.Services.AddZLinkMonitoring(monitor =>
        {
            // The channel is a RouteMesh in 10.0.0: wire-level identity comes
            // from the mesh runtime event stream (spec 50), not socket sources.
            monitor.AddMeshNodeEvents(RuntimeMonitoringNames.Channel);
            monitor.AddMeshNodeEvents(RuntimeMonitoringNames.SpotChannel);
            if (!string.IsNullOrWhiteSpace(options.RedisEndpoint))
                monitor.AddLocationRuntimeEvents(
                    RuntimeMonitoringNames.LocationRuntimeSource,
                    TimeSpan.FromMilliseconds(100));
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapGet("/runtime/snapshot", (
            [FromServices] IZLinkRouteMeshRuntime runtime) =>
            Results.Ok(Project(runtime.Snapshot(RuntimeMonitoringNames.SpotChannel))));
        app.MapGet("/runtime/snapshot/{meshName}", (
            string meshName,
            [FromServices] IZLinkRouteMeshRuntime runtime) =>
            Results.Ok(Project(runtime.Snapshot(meshName))));
        app.MapPost("/runtime/observer/start/{meshName}", (
            string meshName,
            [FromServices] ObserverIsolationProbe probe) =>
        {
            probe.Start(meshName);
            return Results.Ok(probe.Status());
        });
        app.MapPost("/runtime/observer/release", (
            [FromServices] ObserverIsolationProbe probe) =>
        {
            probe.ReleaseSlowConsumer();
            return Results.Ok(probe.Status());
        });
        app.MapGet("/runtime/observer/status", (
            [FromServices] ObserverIsolationProbe probe) =>
            Results.Ok(probe.Status()));
        app.MapGet("/runtime/validate", async (
            [FromServices] IZLinkRouteMeshRuntime runtime,
            CancellationToken cancellationToken) =>
        {
            var missingSnapshotRejected = Rejects(
                () => runtime.Snapshot("missing.mesh"));
            var missingObserverRejected = await RejectsObserverAsync(
                runtime,
                "missing.mesh",
                capacity: 1,
                cancellationToken);
            var zeroCapacityRejected = await RejectsObserverAsync(
                runtime,
                RuntimeMonitoringNames.SpotChannel,
                capacity: 0,
                cancellationToken);
            return Results.Ok(new RuntimeValidationRes(
                missingSnapshotRejected,
                missingObserverRejected,
                zeroCapacityRejected));
        });
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
        app.MapPost("/spot/profile/request", async (
            ProfileReq request,
            [FromServices] IZLinkRouteClient channel,
            CancellationToken cancellationToken) =>
        {
            var response = await channel.RequestToChannel(
                    RuntimeMonitoringNames.SpotChannel,
                    RuntimeMonitoringNames.SpotChannel,
                    request)
                .Timeout(TimeSpan.FromSeconds(30))
                .Async<ProfileRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapPost("/spot/publish/{topic}", async (
            string topic,
            ProfileReq request,
            [FromServices] IZLinkSpotPublisherClient publisher,
            CancellationToken cancellationToken) =>
        {
            await publisher.Publish(
                    RuntimeMonitoringNames.SpotChannel,
                    RuntimeMonitoringNames.SpotChannel,
                    topic,
                    request)
                .Async(cancellationToken);
            return Results.Ok();
        });
        app.MapPost("/admin/application-gate/reset", (
            [FromServices] ApplicationDispatchGate gate) =>
        {
            gate.Reset();
            return Results.Ok(new { status = "reset" });
        });
        app.MapPost("/admin/application-gate/release", (
            [FromServices] ApplicationDispatchGate gate) =>
        {
            gate.Release();
            return Results.Ok(new { status = "released" });
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
        app.MapPost("/admin/subject/create/{spotRid}", async (
            string spotRid,
            [FromServices] IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            await spots.GetOrCreateAsync<MonitoringSubjectSpot>(
                RoutingId.From(spotRid),
                cancellationToken);
            return Results.Ok(new { status = "created", spotRid });
        });
        app.MapPost("/admin/subject/close", async (
            [FromServices] IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var closed = await spots.CloseAsync(RoutingId.From("monitor-subject"), cancellationToken);
            return Results.Ok(new { status = closed ? "closed" : "not-found" });
        });
        app.MapPost("/admin/subject/close/{spotRid}", async (
            string spotRid,
            [FromServices] IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var closed = await spots.CloseAsync(
                RoutingId.From(spotRid),
                cancellationToken);
            return Results.Ok(new
            {
                status = closed ? "closed" : "not-found",
                spotRid
            });
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
        app.MapPost("/admin/spot-weight/exclude", (
            [FromServices] IZLinkRouteMeshRuntimeOptions runtimeOptions) =>
        {
            runtimeOptions.Channel(
                RuntimeMonitoringNames.SpotChannel,
                RuntimeMonitoringNames.SpotChannel).Weight = 0;
            return Results.Ok(new { status = "drained", weight = 0 });
        });
        app.MapPost("/admin/spot-weight/include", (
            [FromServices] IZLinkRouteMeshRuntimeOptions runtimeOptions) =>
        {
            runtimeOptions.Channel(
                RuntimeMonitoringNames.SpotChannel,
                RuntimeMonitoringNames.SpotChannel).Weight = 100;
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

    private static bool Rejects(Action action)
    {
        try
        {
            action();
            return false;
        }
        catch (Exception exception) when (
            exception is ArgumentException
                or InvalidOperationException
                or KeyNotFoundException)
        {
            return true;
        }
    }

    private static async Task<bool> RejectsObserverAsync(
        IZLinkRouteMeshRuntime runtime,
        string meshName,
        int capacity,
        CancellationToken cancellationToken)
    {
        try
        {
            await using var observer = runtime.ObserveAsync(
                    meshName,
                    capacity,
                    cancellationToken)
                .GetAsyncEnumerator(cancellationToken);
            _ = await observer.MoveNextAsync();
            return false;
        }
        catch (Exception exception) when (
            exception is ArgumentException
                or InvalidOperationException
                or KeyNotFoundException)
        {
            return true;
        }
    }

    private static MeshRuntimeSnapshotRes Project(ZLinkMeshNodeSnapshot snapshot)
    {
        return new MeshRuntimeSnapshotRes(
            snapshot.MeshName,
            snapshot.Rid.ToString(),
            snapshot.LifecycleGeneration,
            snapshot.DescriptorRevision,
            snapshot.Endpoint,
            snapshot.State.ToString(),
            snapshot.Sequence,
            snapshot.ObservedAt,
            [.. snapshot.DescriptorSources],
            snapshot.Peers.Select(static peer => new MeshRuntimePeerRes(
                peer.Rid.ToString(),
                peer.LifecycleGeneration,
                peer.DescriptorRevision,
                peer.Endpoint,
                peer.AdmissionState,
                peer.Ready,
                peer.DrainState,
                [.. peer.ChannelNames],
                peer.LastFailure)).ToArray(),
            snapshot.Channels.Select(static channel => new MeshRuntimeChannelRes(
                channel.ChannelName,
                channel.LocalWeight,
                channel.ReadyMemberCount,
                channel.Selectable)).ToArray(),
            new MeshRuntimeClaimsRes(
                snapshot.Claims.ApplicationActive,
                snapshot.Claims.PendingApplicationWork,
                snapshot.Claims.InfrastructureActive,
                snapshot.Claims.PendingInfrastructureWork),
            new MeshRuntimeLocationRes(
                snapshot.Location.State,
                snapshot.Location.LastSuccessAt,
                snapshot.Location.LastFailureAt),
            new MeshRuntimeDrainRes(
                snapshot.Drain.State.ToString(),
                snapshot.Drain.Deadline,
                snapshot.Drain.WorkSealed,
                snapshot.Drain.PendingRequestCount,
                snapshot.Drain.PendingTransferCount,
                snapshot.Drain.PendingStreamBarrierCount));
    }
}
