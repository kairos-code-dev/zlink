using Microsoft.Extensions.Configuration;

using ObservabilityOps.Server.Play.Infrastructure;
using ObservabilityOps.Server.Play.Spots;
using ObservabilityOps.Server.Play.Support;
using ObservabilityOps.Server.Support;
using ObservabilityOps.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Locations.Redis;

namespace ObservabilityOps.Server.Play;

internal static class PlayHostFactory
{
    private const string RoomSpotType = "observability-room";

    public static WebApplication Create(string[] args)
    {
        var options = PlayOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);
        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console => console.SingleLine = true);
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton<EvidenceStore>();
        builder.Services.AddSingleton<DrainOperation>();
        builder.Services.AddSingleton<BoundedOperationGate>();
        if (options.MetricsEnabled) builder.Services.AddSingleton<MetricEvidenceCollector>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.DefaultRequestTimeout = TimeSpan.FromSeconds(15);
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint).SetKeyPrefix(options.RedisKeyPrefix)));
            var locations = framework.ConfigureLocations();
            locations.RouteCacheMaxAge = TimeSpan.Zero;
            locations.MessageFollowDuration = TimeSpan.FromSeconds(5);
            locations.OwnerLeaseRenewInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
            locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
            locations.PollingInterval = TimeSpan.FromMilliseconds(250);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkRuntimeMessageFlowMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"flow-{options.Rid}.log"))
                .TraceLabel(options.Rid);
            framework.AddHandlersFromAssemblyOf(typeof(PlayHostFactory));
            var mesh18 = framework.AddRouteMesh(ObservabilityNames.PlayMesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .SetEntrySpotRoutingId(RoutingId.From(options.Rid));
            mesh18.Objects().Server()
                .AddEntrySpot<PlayEntrySpot>()
                .AddActorFactory<PlayerActor, PlayerActorFactory>(
                    ObservabilityNames.PlayerActorType,
                    null,
                    ZLinkRelocationPolicy<PlayerActor>
                        .Snapshot<PlayerActorRelocationAdapter>())
                .AddSpotFactory<RoomSpot>(
                    RoomSpotType,
                    null,
                    ZLinkRelocationPolicy<RoomSpot>.Disabled);
            mesh18.ChannelName(ObservabilityNames.PlayMesh);
        });

        var app = builder.Build();
        if (options.MetricsEnabled) _ = app.Services.GetRequiredService<MetricEvidenceCollector>();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapPost("/message-flow/off", (IZLinkMessageFlowRuntime flow) =>
        {
            flow.Mode = ZLinkRuntimeMessageFlowMode.Off;
            return Results.Ok(new { mode = "off" });
        });
        app.MapPost("/rooms", async (CreateRoomReq request, IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var created = await spots.GetOrCreateAsync<RoomSpot, CreateRoomReq>(
                RoutingId.From(request.RoomRid), request, cancellationToken);
            return Results.Ok(new CreateRoomRes(created.SpotRid.ToString(), options.Rid));
        });
        app.MapPost("/rooms/{roomRid}/close", async (string roomRid, IZLinkSpotManager spots,
            CancellationToken cancellationToken) => Results.Ok(new
            {
                closed = await spots.CloseAsync(RoutingId.From(roomRid), cancellationToken)
            }));
        app.MapGet("/evidence", CreateEvidenceAsync);
        app.MapPost("/evidence/wait", async (EvidenceWaitReq request, EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var timeout = TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000));
            var entries = await evidence.WaitAsync(snapshot => Matches(snapshot, request), timeout, cancellationToken);
            return Results.Ok(entries);
        });
        if (options.MetricsEnabled)
            app.MapPost("/metrics/wait", async (MetricWaitReq request, MetricEvidenceCollector metrics,
                CancellationToken cancellationToken) => Results.Ok(await metrics.WaitAsync(
                samples => MetricWait.Matches(samples, request),
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                cancellationToken)));
        app.MapDrainOperations();
        app.MapBoundedOperationGate();
        app.MapPost("/operation/start", async (
            PlayBoundedOperationReq request,
            IZLinkSpotClient routes,
            IZLinkSpotHandleResolver spots,
            CancellationToken cancellationToken) =>
        {
            var entry = await spots.ResolveSpotHandleAsync(
                            ObservabilityNames.PlayMesh,
                            RoutingId.From(options.Rid),
                            cancellationToken)
                        ?? throw new InvalidOperationException("The local Play entry spot was not found.");
            var response = await routes.RequestToSpot(entry, request)
                .Async<PlayBoundedOperationRes>(cancellationToken);
            return Results.Ok(response);
        });
        return app;

        async Task<IResult> CreateEvidenceAsync(
            IZLinkDrainControl drain,
            IZLinkLocationRuntimeQuery locations,
            IZLinkSpotHandleResolver spots,
            IZLinkActorManager actors,
            EvidenceStore evidence,
            IServiceProvider services,
            string? spotRid,
            string? actorId,
            CancellationToken cancellationToken)
        {
            var peers = await locations.ListMeshNodeDescriptorsAsync(
                ObservabilityNames.PlayMesh, cancellationToken);
            // Spot and actor rows are resolve-only store records in 10.0.0:
            // the operational surface enumerates MeshNode descriptors and
            // resolves a caller-named spot rid on request.
            var actorRows = Array.Empty<ActorRow>();
            if (!string.IsNullOrWhiteSpace(actorId)
                && await actors.FindAsync(actorId, cancellationToken) is { } actorRef)
                actorRows =
                [
                    new ActorRow(actorId, actorRef.NodeRid.ToString(), (long)actorRef.Generation)
                ];
            var spotRows = Array.Empty<SpotRow>();
            if (!string.IsNullOrWhiteSpace(spotRid)
                && await spots.ResolveSpotHandleAsync(
                        ObservabilityNames.PlayMesh,
                        RoutingId.From(spotRid),
                        cancellationToken)
                    is { } handle)
                spotRows = [new SpotRow(ObservabilityNames.PlayMesh, options.Rid, handle.SpotRid.ToString(), "spot", 0)];
            return Results.Ok(new EvidenceSnapshot(
                options.Rid, drain.IsReady, evidence.Snapshot(),
                services.GetService<MetricEvidenceCollector>()?.Snapshot() ?? [],
                peers.Select(row => new PeerRow(row.Rid.ToString(),
                    row.Draining, (long)row.LifecycleGeneration)).ToArray(),
                actorRows,
                spotRows));
        }
    }

    private static bool Matches(string[] entries, EvidenceWaitReq request) =>
        request.ContainsAll.All(expected => entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
        && request.ContainsAnyGroups.All(group => group.Any(expected =>
            entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal))));
}
