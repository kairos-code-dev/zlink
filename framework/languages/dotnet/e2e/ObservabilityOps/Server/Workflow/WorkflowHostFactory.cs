using Microsoft.Extensions.Configuration;

using ObservabilityOps.Server.Support;
using ObservabilityOps.Server.Workflow.Spots;
using ObservabilityOps.Server.Workflow.Support;
using ObservabilityOps.Shared;
using StackExchange.Redis;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Locations.Redis;

namespace ObservabilityOps.Server.Workflow;

internal static class WorkflowHostFactory
{
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
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint).SetKeyPrefix(options.RedisKeyPrefix)));
            var locations = framework.ConfigureLocations();
            locations.HeartbeatInterval = TimeSpan.FromMilliseconds(options.LocationHeartbeatMs);
            locations.OwnerLeaseTtl = TimeSpan.FromMilliseconds(options.LocationLeaseTtlMs);
            framework.ConfigureDispatch().MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"flow-{options.Rid}.log"))
                .TraceLabel(options.Rid);
            framework.AddHandlersFromAssemblyOf(typeof(WorkflowHostFactory));
            var mesh16 = framework.AddRouteMesh(ObservabilityNames.WorkflowMesh)
                .UseDrainPolicy(ZLinkMeshNodeDrainPolicy.ReleaseAndRecreate)
                .Listen(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddSpotFactory<WorkflowSpot>()
                .AddSpotFactory<ProjectionSpot>();
            mesh16.ChannelName(ObservabilityNames.WorkflowMesh);
        });

        var app = builder.Build();
        _ = app.Services.GetRequiredService<MetricEvidenceCollector>();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapPost("/workflows", async (CreateWorkflowReq request, IZLinkSpotManager spots,
            CancellationToken cancellationToken) =>
        {
            var created = string.Equals(request.Kind, "subscriber", StringComparison.Ordinal)
                ? await spots.GetOrCreateAsync<ProjectionSpot, CreateWorkflowReq>(
                    RoutingId.From(request.WorkflowRid), request, cancellationToken)
                : await spots.GetOrCreateAsync<WorkflowSpot, CreateWorkflowReq>(
                    RoutingId.From(request.WorkflowRid), request, cancellationToken);
            return Results.Ok(new CreateWorkflowRes(created.SpotRid.ToString(), options.Rid, 0, "created"));
        });
        app.MapPost("/workflows/{workflowRid}/advance", async (string workflowRid,
            AdvanceWorkflowReq request, IZLinkSpotHandleResolver resolver, IZLinkRouteClient routes,
            CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(RoutingId.From(workflowRid), cancellationToken)
                         ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var response = await routes.RequestToSpot(handle, request).Async<AdvanceWorkflowRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapGet("/workflows/{workflowRid}/state", async (string workflowRid,
            IZLinkSpotHandleResolver resolver, IZLinkRouteClient routes, CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(RoutingId.From(workflowRid), cancellationToken)
                         ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var response = await routes.RequestToSpot(handle, new ReadWorkflowReq())
                .Async<ReadWorkflowRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapPost("/workflows/{workflowRid}/publish", async (string workflowRid,
            PublishProjectionReq request, IZLinkSpotHandleResolver resolver, IZLinkRouteClient routes,
            CancellationToken cancellationToken) =>
        {
            var handle = await resolver.ResolveSpotHandleAsync(RoutingId.From(workflowRid), cancellationToken)
                         ?? throw new InvalidOperationException($"Workflow '{workflowRid}' was not found.");
            var response = await routes.RequestToSpot(handle, request).Async<PublishProjectionRes>(cancellationToken);
            return Results.Ok(response);
        });
        app.MapGet("/evidence", async (IZLinkDrainControl drain, IZLinkLocationRuntimeQuery locations,
            WorkflowEvidenceStore evidence, MetricEvidenceCollector metrics) =>
        {
            var peers = await locations.ListMeshNodeDescriptorsAsync(ObservabilityNames.WorkflowMesh);
            // Spot rows are resolve-only store records in 10.0.0: the
            // operational surface enumerates MeshNode descriptors only.
            return Results.Ok(new EvidenceSnapshot(options.Rid, drain.IsReady, evidence.Snapshot(),
                metrics.Snapshot(), peers.Select(row => new PeerRow(row.Rid.ToString(),
                    row.Draining, (long)row.LifecycleGeneration)).ToArray(), [],
                []));
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
}
