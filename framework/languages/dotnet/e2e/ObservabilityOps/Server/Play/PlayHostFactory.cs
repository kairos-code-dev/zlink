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
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Locations.Redis;

namespace ObservabilityOps.Server.Play;

internal static class PlayHostFactory
{
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
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint).SetKeyPrefix(options.RedisKeyPrefix)));
            var locations = framework.ConfigureLocations();
            locations.HeartbeatInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
            locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
            locations.PollingInterval = TimeSpan.FromMilliseconds(250);
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"flow-{options.Rid}.log"))
                .TraceLabel(options.Rid);
            framework.AddHandlersFromAssemblyOf(typeof(PlayHostFactory));
            framework.AddSpotMesh(ObservabilityNames.PlayMesh)
                .UseDrainPolicy(ZLinkSpotDrainPolicy.DrainNatural)
                .EnableRouter(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .SetEntrySpotRoutingId(RoutingId.From(options.Rid))
                .EnablePubSub(options.PubEndpoint)
                .AddEntrySpot<PlayEntrySpot>()
                .AddActorFactory<PlayerActorFactory>(ObservabilityNames.PlayerActorType)
                .AddActorTransferAdapter<PlayerActor, PlayerActorTransferAdapter>(ObservabilityNames.PlayerActorType)
                .AddSpotFactory<RoomSpot>();
        });

        var app = builder.Build();
        if (options.MetricsEnabled) _ = app.Services.GetRequiredService<MetricEvidenceCollector>();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapPost("/message-flow/off", (IZLinkMessageFlowControl flow) =>
        {
            flow.SetMessageFlowMode(ZLinkMessageFlowLogMode.Off);
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
            IZLinkRouteClient routes,
            IZLinkSpotHandleResolver spots,
            CancellationToken cancellationToken) =>
        {
            var entry = await spots.ResolveSpotHandleAsync(RoutingId.From(options.Rid), cancellationToken)
                        ?? throw new InvalidOperationException("The local Play entry spot was not found.");
            var response = await routes.RequestToSpot(entry, request)
                .Async<PlayBoundedOperationRes>(cancellationToken);
            return Results.Ok(response);
        });
        return app;

        async Task<IResult> CreateEvidenceAsync(
            IZLinkDrainControl drain,
            IZLinkLocationRuntimeQuery locations,
            EvidenceStore evidence,
            IServiceProvider services,
            CancellationToken cancellationToken)
        {
            var peers = await locations.ListPeerLocationsAsync(new ZLinkPeerLocationFilter(), cancellationToken);
            var actors = (await locations.ListActorLocationsAsync(
                new ZLinkActorLocationFilter())).Items;
            var spots = (await locations.ListSpotLocationsAsync(
                new ZLinkSpotLocationFilter())).Items;
            return Results.Ok(new EvidenceSnapshot(
                options.Rid, drain.IsReady, evidence.Snapshot(),
                services.GetService<MetricEvidenceCollector>()?.Snapshot() ?? [],
                peers.Select(row => new PeerRow(row.NodeRid?.ToString() ?? string.Empty,
                    row.Draining, row.Generation)).ToArray(),
                actors.Select(row => new ActorRow(row.ActorId, row.NodeRid.ToString(), row.Generation)).ToArray(),
                spots.Select(row => new SpotRow(row.MeshName, row.NodeRid.ToString(),
                    row.SpotRid.ToString(), row.SpotKind.ToString(), row.Generation)).ToArray()));
        }
    }

    private static bool Matches(string[] entries, EvidenceWaitReq request) =>
        request.ContainsAll.All(expected => entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
        && request.ContainsAnyGroups.All(group => group.Any(expected =>
            entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal))));
}
