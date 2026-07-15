using RuntimeMonitoring.Server.Service.Handlers;
using RuntimeMonitoring.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Locations.Redis;

namespace RuntimeMonitoring.Server.Service.Support;

internal sealed class ChannelMonitoringRoleHost
{
    private readonly WebApplicationBuilder _builder;
    private readonly ServerOptions _options;

    private ChannelMonitoringRoleHost(string[] args, string defaultRole)
    {
        _options = ServerOptions.Parse(args, defaultRole);
        _builder = WebApplication.CreateBuilder(args);
        _builder.WebHost.UseUrls(_options.HttpUrl);
        _builder.Services.AddSingleton(new EvidenceStore(_options.EvidenceFile, _options.Rid));
        _builder.Services.AddSingleton<LocationTopologyTransitionTracker>();
        _builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, SocketEventRecorder>();
        _builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkLocationRuntimeEvent>, LocationRuntimeEventRecorder>();
        _builder.Services.AddZLinkFramework(framework =>
        {
            if (!string.IsNullOrWhiteSpace(_options.RedisEndpoint))
                framework.AddLocationStore(new ZLinkRedisLocationStore(redis => redis
                    .SetConnectionString(_options.RedisEndpoint)
                    .SetKeyPrefix(Require(_options.RedisKeyPrefix, "--redis-key-prefix"))));
            framework.AddClientServerChannel(RuntimeMonitoringNames.Channel)
                .EnableServer(Require(_options.ChannelEndpoint, "--channel-endpoint"))
                .SetRoutingId(RoutingId.From(_options.Rid))
                .AddRequestHandler<ProfileRequestHandler, ProfileReq, ProfileRes>("ProfileReq");
        });
    }

    public static ChannelMonitoringRoleHost Create(string[] args, string defaultRole)
    {
        return new ChannelMonitoringRoleHost(args, defaultRole);
    }

    public void AddSocketEventHandler<THandler>()
        where THandler : class, IZLinkRuntimeEventHandler<ZLinkSocketEvent>
    {
        _builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, THandler>();
    }

    public void ConfigureMonitoring(params ZLinkSocketEventKind[] socketEvents)
    {
        _builder.Services.AddZLinkMonitoring(monitor =>
        {
            monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelServerSource, socketEvents);
            if (!string.IsNullOrWhiteSpace(_options.RedisEndpoint))
                monitor.AddLocationRuntimeEvents(
                    RuntimeMonitoringNames.LocationRuntimeSource,
                    TimeSpan.FromMilliseconds(100));
        });
    }

    public WebApplication Build()
    {
        var app = _builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", _options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        app.MapPost("/evidence/wait", async (
            EvidenceWaitReq request,
            EvidenceStore evidence,
            CancellationToken cancellationToken) =>
        {
            var snapshot = await evidence.WaitUntilAsync(
                entries => request.ContainsAll.All(expected => entries.Skip(request.AfterIndex)
                    .Any(entry => entry.Contains(expected, StringComparison.Ordinal))),
                TimeSpan.FromMilliseconds(Math.Clamp(request.TimeoutMilliseconds, 1, 30000)),
                cancellationToken);
            return Results.Ok(snapshot.Skip(request.AfterIndex).ToArray());
        });
        app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
        {
            lifetime.StopApplication();
            return Results.Ok(new { status = "stopping" });
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
