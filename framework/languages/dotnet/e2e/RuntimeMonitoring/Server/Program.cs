using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RuntimeMonitoring.Shared;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Eventing;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Timers;

var options = ServerOptions.Parse(args);
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
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkRegistryEvent>, RegistryEventRecorder>();
builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSpotEvent>, SpotEventRecorder>();
if (options.MonitorMode == "throwing")
{
    builder.Services.AddScoped<IZLinkRuntimeEventHandler<ZLinkSocketEvent>, ThrowingSocketEventRecorder>();
}

if (options.Role == "registry")
{
    builder.Services.AddZLinkRegistry(registry =>
    {
        registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
        registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
        registry.BroadcastInterval = TimeSpan.FromMilliseconds(250);
    });
    builder.Services.AddZLinkMonitoring(monitor =>
    {
        monitor.AddRegistryEvents("registry", TimeSpan.FromMilliseconds(250));
    });
}
else if (options.Role == "service")
{
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        framework.ConfigureDispatch()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceNodeId(options.Rid);

        var channel = framework.AddClientServerChannel(RuntimeMonitoringNames.Channel)
            .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"));
        channel.ConfigureServerRouting().RoutingId = RoutingId.From(options.Rid);
        channel.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");

        if (options.MonitorMode == "all")
        {
            var spotMesh = framework.AddSpotMesh(RuntimeMonitoringNames.SpotChannel);
            spotMesh.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
            spotMesh.EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
                .SetRouterRoutingId(RoutingId.From(options.Rid))
                .EnablePubSub(Require(options.SpotPubEndpoint, "--spot-pub-endpoint"))
                .SetPubSubRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<MonitoringEntrySpot>();
        }
    });
    builder.Services.AddZLinkMonitoring(monitor =>
    {
        if (options.MonitorMode == "socket-filter")
        {
            monitor.AddSocketEvents(
                RuntimeMonitoringNames.ChannelServerSource,
                ZLinkSocketEventKind.ConnectionReady);
        }
        else
        {
            monitor.AddSocketEvents(RuntimeMonitoringNames.ChannelServerSource);
        }

        if (options.MonitorMode == "all")
        {
            monitor.AddSpotEvents(RuntimeMonitoringNames.SpotNode, TimeSpan.FromMilliseconds(100));
        }
    });
}
else
{
    throw new InvalidOperationException($"Unsupported role '{options.Role}'.");
}

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
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
await app.RunAsync();

static string Require(string? value, string name)
{
    return string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{name} is required.")
        : value;
}

internal sealed class ProfileRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"profile-request|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
        return ValueTask.FromResult(new ProfileReply($"profile:{request.Value}", evidence.Rid, request.Marker));
    }
}

internal sealed class MonitoringEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public async ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        await Context.AddTimer<FailingTimerHandler>(
            "failing",
            TimeSpan.FromMilliseconds(50),
            new ZLinkTimerOptions { StopOnUnhandledException = false },
            cancellationToken);
        await Context.AddTimer<FailingTimerHandler>(
            "stopping",
            TimeSpan.FromMilliseconds(50),
            new ZLinkTimerOptions { StopOnUnhandledException = true },
            cancellationToken);
    }
}

internal sealed class FailingTimerHandler : IZLinkSpotTimerHandler<MonitoringEntrySpot>
{
    public ValueTask HandleAsync(
        MonitoringEntrySpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        throw new InvalidOperationException("monitoring timer failure");
    }
}

internal sealed class SocketEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-socket|source={@event.SourceName}|kind={@event.Event}"
            + $"|remote={@event.RemoteAddr}|routing={@event.RoutingId}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ThrowingSocketEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSocketEvent>
{
    public ValueTask HandleAsync(ZLinkSocketEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"monitor-throw|source={@event.SourceName}|kind={@event.Event}");
        throw new InvalidOperationException("monitoring dispatch failure for e2e");
    }
}

internal sealed class RegistryEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkRegistryEvent>
{
    public ValueTask HandleAsync(ZLinkRegistryEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-registry|source={@event.SourceName}|kind={@event.Event}"
            + $"|topology={@event.Topology?.Count ?? -1}|summary={@event.ServiceSummary?.Count ?? -1}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class SpotEventRecorder(EvidenceStore evidence) : IZLinkRuntimeEventHandler<ZLinkSpotEvent>
{
    public ValueTask HandleAsync(ZLinkSpotEvent @event, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"monitor-spot|source={@event.SourceName}|kind={@event.Event}"
            + $"|peers={@event.Peers?.Count ?? -1}|subjects={@event.Subjects?.Count ?? -1}"
            + $"|timer={@event.TimerDiagnostic?.TimerName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;

    public EvidenceStore(string? filePath)
    {
        _filePath = filePath;
        Rid = Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node";
        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            Directory.CreateDirectory(Path.GetDirectoryName(_filePath)!);
            File.WriteAllText(_filePath, string.Empty);
        }
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        _entries.Enqueue(entry);
        if (string.IsNullOrWhiteSpace(_filePath))
        {
            return;
        }

        lock (_fileGate)
        {
            File.AppendAllText(_filePath, entry + Environment.NewLine);
        }
    }

    public string[] Snapshot() => _entries.ToArray();
}

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? SpotRouterEndpoint,
    string? SpotPubEndpoint,
    string MonitorMode)
{
    public static ServerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            values[key] = args[++i];
        }

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var value)
            ? value
            : fallback;

        return new ServerOptions(
            Role: Get("--role", "service"),
            HttpUrl: Get("--http-url", "http://127.0.0.1:0"),
            LogDir: Get("--log-dir", "logs"),
            EvidenceFile: Get("--evidence-file"),
            Rid: Get("--rid", Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node"),
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ChannelEndpoint: Get("--channel-endpoint"),
            SpotRouterEndpoint: Get("--spot-router-endpoint"),
            SpotPubEndpoint: Get("--spot-pub-endpoint"),
            MonitorMode: Get("--monitor-mode", "all"));
    }
}
