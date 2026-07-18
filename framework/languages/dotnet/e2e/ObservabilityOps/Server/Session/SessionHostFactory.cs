using Microsoft.Extensions.Configuration;

using ObservabilityOps.Server.Session.Sessions;
using ObservabilityOps.Server.Session.Support;
using ObservabilityOps.Server.Support;
using ObservabilityOps.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Locations.Redis;

namespace ObservabilityOps.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = ObservabilityOps.Server.Session.Support.SessionOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);
        var builder = WebApplication.CreateBuilder(args);
        builder.Configuration.Sources.Clear();
        builder.Configuration.AddInMemoryCollection();
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console => console.SingleLine = true);
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(options);
        builder.Services.AddSingleton<MetricEvidenceCollector>();
        builder.Services.AddSingleton<DrainOperation>();
        builder.Services.AddSingleton<BoundedOperationGate>();
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                .SetConnectionString(options.RedisEndpoint).SetKeyPrefix(options.RedisKeyPrefix)));
            framework.ConfigureDispatch().MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"flow-{options.Rid}.log"))
                .TraceLabel(options.Rid);
            framework.AddHandlersFromAssemblyOf(typeof(SessionHostFactory));
            framework.AddRouteMesh(ObservabilityNames.PlayMesh)
                .Listen(options.RouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddStreamNode(ObservabilityNames.StreamNode)
                .Bind(options.StreamEndpoint)
                .RegisterSession<ObservabilitySession>();
        });
        var app = builder.Build();
        _ = app.Services.GetRequiredService<MetricEvidenceCollector>();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Rid }));
        app.MapPost("/message-flow/off", (IZLinkMessageFlowControl flow) =>
        {
            flow.SetMessageFlowMode(ZLinkMessageFlowLogMode.Off);
            return Results.Ok(new { mode = "off" });
        });
        app.MapGet("/evidence", async (
            MetricEvidenceCollector metrics,
            IZLinkDrainControl drain,
            IZLinkLocationRuntimeQuery locations) =>
        {
            var peers = await locations.ListMeshNodeDescriptorsAsync(ObservabilityNames.PlayMesh);
            return Results.Ok(new EvidenceSnapshot(
                options.Rid, drain.IsReady, [], metrics.Snapshot(),
                peers.Select(row => new PeerRow(row.Rid.ToString(),
                    row.Draining, (long)row.LifecycleGeneration)).ToArray(), [], []));
        });
        app.MapPost("/metrics/wait", async (MetricWaitReq request, MetricEvidenceCollector metrics,
            CancellationToken cancellationToken) => Results.Ok(await metrics.WaitAsync(
            samples => MetricWait.Matches(samples, request),
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                cancellationToken)));
        app.MapDrainOperations();
        app.MapBoundedOperationGate();
        return app;
    }
}
