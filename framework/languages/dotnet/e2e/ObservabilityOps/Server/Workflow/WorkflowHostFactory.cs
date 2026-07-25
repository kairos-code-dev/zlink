using System.Text;
using Microsoft.Extensions.Configuration;

using ObservabilityOps.Server.Support;
using ObservabilityOps.Server.Workflow.Spots;
using ObservabilityOps.Server.Workflow.Support;
using ObservabilityOps.Shared;
using StackExchange.Redis;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Locations.Redis;

namespace ObservabilityOps.Server.Workflow;

internal static class WorkflowHostFactory
{
    private const string WorkflowSpotType = "observability-workflow";
    private const string ProjectionSpotType = "observability-projection";

    public static WebApplication Create(string[] args)
    {
        var options = WorkflowOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);
        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console => console.SingleLine = true);
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton<IConnectionMultiplexer>(_ =>
            ConnectionMultiplexer.Connect(options.RedisEndpoint));
        builder.Services.AddSingleton<WorkflowStateStore>();
        builder.Services.AddSingleton<WorkflowEvidenceStore>();
        builder.Services.AddSingleton<MetricEvidenceCollector>();
        builder.Services.AddSingleton<DrainOperation>();
        builder.Services.AddSingleton<StaleSpotHandleProbe>();
        var locationStore = new ZLinkRedisLocationStore(redis => redis
            .SetConnectionString(options.RedisEndpoint).SetKeyPrefix(options.RedisKeyPrefix));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(locationStore);
            var locations = framework.ConfigureLocations();
            locations.OwnerLeaseRenewInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
            locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
            framework.ConfigureDispatch().MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"flow-{options.Rid}.log"))
                .TraceLabel(options.Rid);
            framework.AddHandlersFromAssemblyOf(typeof(WorkflowHostFactory));
            var mesh16 = framework.AddRouteMesh(ObservabilityNames.WorkflowMesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            mesh16.Objects().Server()
                .AddSpotFactory<WorkflowSpot>(
                    WorkflowSpotType,
                    null,
                    ZLinkRelocationPolicy<WorkflowSpot>.Disabled)
                .AddSpotFactory<ProjectionSpot>(
                    ProjectionSpotType,
                    null,
                    ZLinkRelocationPolicy<ProjectionSpot>.Disabled);
            mesh16.ChannelName(ObservabilityNames.WorkflowMesh);
        });

        var app = builder.Build();
        _ = app.Services.GetRequiredService<MetricEvidenceCollector>();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapPost("/workflows", async (CreateWorkflowReq request, IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var spotType = string.Equals(
                request.Kind, "subscriber", StringComparison.Ordinal)
                ? ProjectionSpotType
                : WorkflowSpotType;
            var created = await spots.GetOrCreate(request.WorkflowRid, spotType)
                .InMesh(ObservabilityNames.WorkflowMesh)
                .Request(request)
                .Async(cancellationToken);
            return Results.Ok(new CreateWorkflowRes(
                created.Spot.SpotId, options.Rid, 0, "created"));
        });
        app.MapPost("/workflows/{workflowRid}/advance", async (string workflowRid,
            AdvanceWorkflowReq request, IZLinkSpotHandleResolver resolver, IZLinkSpotClient routes,
            CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(
                ObservabilityNames.WorkflowMesh,
                workflowRid,
                cancellationToken)
                ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var response = await routes.RequestToSpot(handle, request).Async<AdvanceWorkflowRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapGet("/workflows/{workflowRid}/state", async (string workflowRid,
            IZLinkSpotHandleResolver resolver, IZLinkSpotClient routes, CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(
                ObservabilityNames.WorkflowMesh,
                workflowRid,
                cancellationToken)
                ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var response = await routes.RequestToSpot(handle, new ReadWorkflowReq())
                .Async<ReadWorkflowRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapPost("/workflows/{workflowRid}/signal", async (string workflowRid,
            WorkflowSignalReq request, IZLinkSpotHandleResolver resolver, IZLinkSpotClient routes,
            CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(
                ObservabilityNames.WorkflowMesh,
                workflowRid,
                cancellationToken)
                ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var submit = await routes.SendToSpot(handle, request).SubmitAsync(cancellationToken);
            return Results.Ok(new
            {
                submitted = submit.Status
            });
        });
        app.MapPost("/workflows/{workflowRid}/stale-handle/capture", async (string workflowRid,
            IZLinkSpotHandleResolver resolver, StaleSpotHandleProbe probe,
            CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(
                ObservabilityNames.WorkflowMesh,
                workflowRid,
                cancellationToken)
                ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            probe.Capture(handle);
            return Results.Ok();
        });
        app.MapPost("/stale-handle/execute", async (StaleSpotHandleProbe probe,
            IZLinkSpotClient routes, CancellationToken cancellationToken) =>
            Results.Ok(await probe.ExecuteAsync(routes, cancellationToken)));
        app.MapPost("/workflows/{workflowRid}/publish", async (string workflowRid,
            PublishProjectionReq request, IZLinkSpotHandleResolver resolver, IZLinkSpotClient routes,
            CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(
                ObservabilityNames.WorkflowMesh,
                workflowRid,
                cancellationToken)
                ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var response = await routes.RequestToSpot(handle, request).Async<PublishProjectionRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapGet("/evidence", async (IZLinkDrainControl drain, IZLinkLocationRuntimeQuery locations,
            WorkflowEvidenceStore evidence, MetricEvidenceCollector metrics, string? spotRid) =>
        {
            // Spot rows are resolve-only store records in 10.0.0: the
            // operational surface enumerates MeshNode descriptors only. The
            // evidence endpoint must stay observable through a store outage
            // (OBS-B3 pauses Redis and reads the lease-lateness metrics), so
            // a failing row read degrades to an empty peer list.
            IReadOnlyList<Zlink.Framework.Contracts.Locations.ZLinkMeshNodeDescriptor> peers;
            try
            {
                // Evidence must answer fast even while the store is paused
                // (OBS-B3 polls the lease metrics through the outage); the
                // row listing is auxiliary, so it gets a short bound.
                peers = await locations.ListMeshNodeDescriptorsAsync(ObservabilityNames.WorkflowMesh)
                    .AsTask().WaitAsync(TimeSpan.FromMilliseconds(500));
            }
            catch (Exception)
            {
                peers = [];
            }
            var spotRows = Array.Empty<SpotRow>();
            if (!string.IsNullOrWhiteSpace(spotRid))
            {
                try
                {
                    if (await locationStore.ReadAuthorityAsync(SpotAuthorityKey(spotRid))
                            .AsTask().WaitAsync(TimeSpan.FromMilliseconds(500))
                        is ZLinkAuthorityReadResult.Found { Snapshot: var row })
                        spotRows =
                        [
                            new SpotRow(
                                row.Allocation.Descriptor.MeshName,
                                row.Allocation.Descriptor.Rid.ToString(),
                                spotRid,
                                row.Allocation.ObjectKind.ToString(),
                                checked((long)row.ObjectGeneration))
                        ];
                }
                catch (Exception)
                {
                    // Resolve-only observation degrades with the store.
                }
            }

            return Results.Ok(new EvidenceSnapshot(options.Rid, drain.IsReady, evidence.Snapshot(),
                metrics.Snapshot(), peers.Select(row => new PeerRow(row.Rid.ToString(),
                    row.Draining, (long)row.LifecycleGeneration)).ToArray(), [],
                spotRows));
        });
        app.MapPost("/evidence/wait", async (EvidenceWaitReq request, WorkflowEvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var entries = await evidence.WaitAsync(snapshot => Matches(snapshot, request),
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)), cancellationToken);
            return Results.Ok(entries);
        });
        app.MapPost("/metrics/wait", async (MetricWaitReq request, MetricEvidenceCollector metrics,
            CancellationToken cancellationToken) => Results.Ok(await metrics.WaitAsync(
            samples => MetricWait.Matches(samples, request),
            TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
            cancellationToken)));
        app.MapDrainOperations();
        return app;
    }

    private static bool Matches(string[] entries, EvidenceWaitReq request) =>
        request.ContainsAll.All(expected => entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal)))
        && request.ContainsAnyGroups.All(group => group.Any(expected =>
            entries.Any(entry => entry.Contains(expected, StringComparison.Ordinal))));

    private static ZLinkAuthorityKey SpotAuthorityKey(string spotId)
    {
        var bytes = new UTF8Encoding(false, true).GetBytes(spotId);
        var builder = new StringBuilder($"zla1:s:{bytes.Length}:");
        foreach (var item in bytes)
        {
            if (item is >= (byte)'A' and <= (byte)'Z'
                or >= (byte)'a' and <= (byte)'z'
                or >= (byte)'0' and <= (byte)'9'
                or (byte)'-' or (byte)'.' or (byte)'_' or (byte)'~')
                builder.Append((char)item);
            else
                builder.Append('%').Append(item.ToString("X2"));
        }
        return new ZLinkAuthorityKey(builder.ToString());
    }
}
