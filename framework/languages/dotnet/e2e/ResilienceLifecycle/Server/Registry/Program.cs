using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using ResilienceLifecycle.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;

var options = ServerOptions.Parse(args, defaultRole: "registry");
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
builder.Services.AddSingleton<FaultState>();

if (options.Role == "registry")
{
    builder.Services.AddZLinkRegistry(registry =>
    {
        registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
        registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
    });
}
else if (options.Role == "provider")
{
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        framework.ConfigureDispatch()
            .SetMessageFlowObserver<EvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceLabel(options.Rid);
        var channel = framework.AddClientServerChannel(ResilienceLifecycleNames.Channel)
            .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
            .SetRoutingId(RoutingId.From(options.Rid));
        channel.ConfigureServerSocket().Weight = options.Weight;
        channel.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
        channel.AddSendHandler<ProfileCommandHandler, ProfileCommand>("ProfileCommand");
    });
}
else
{
    throw new InvalidOperationException($"Unsupported role '{options.Role}'.");
}

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
app.MapPost("/evidence/clear", (EvidenceStore evidence) =>
{
    evidence.Clear();
    return Results.Ok(new { status = "cleared" });
});
app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
{
    lifetime.StopApplication();
    return Results.Ok(new { status = "stopping" });
});
app.MapPost("/admin/crash", () =>
{
    _ = Task.Run(async () =>
    {
        await Task.Delay(50);
        Environment.FailFast("resilience lifecycle e2e crash");
    });
    return Results.Ok(new { status = "crashing" });
});
app.MapPost("/admin/fault/{mode}", (
    string mode,
    [FromServices] FaultState fault,
    [FromServices] EvidenceStore evidence) =>
{
    fault.Mode = mode;
    evidence.Add($"admin|rid={evidence.Rid}|action=fault|mode={mode}");
    return Results.Ok(new { status = "fault", mode });
});
app.MapPost("/admin/drain", (
    [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
    [FromServices] EvidenceStore evidence) =>
{
    runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel).ConfigureServerSocket().Weight = 0;
    evidence.Add($"admin|rid={evidence.Rid}|action=drain|weight=0");
    return Results.Ok(new { status = "drained", weight = 0 });
});
app.MapPost("/admin/restore", (
    [FromServices] IZLinkChannelRuntimeOptions runtimeOptions,
    [FromServices] EvidenceStore evidence) =>
{
    runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel).ConfigureServerSocket().Weight = 100;
    evidence.Add($"admin|rid={evidence.Rid}|action=restore|weight=100");
    return Results.Ok(new { status = "restored", weight = 100 });
});
app.MapGet("/admin/weight", ([FromServices] IZLinkChannelRuntimeOptions runtimeOptions) =>
{
    var weight = runtimeOptions.ClientServerChannel(ResilienceLifecycleNames.Channel).ConfigureServerSocket().Weight;
    return Results.Ok(new { weight });
});
await app.RunAsync();

static string Require(string? value, string name)
{
    return string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{name} is required.")
        : value;
}

internal sealed class ProfileRequestHandler(EvidenceStore evidence, FaultState fault)
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public async ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (fault.Mode == "observer-throws")
        {
            throw new InvalidOperationException("observer isolation trigger");
        }

        if (fault.Mode == "gray" && request.Value == "gray")
        {
            evidence.Add($"profile-fault|rid={evidence.Rid}|marker={request.Marker}|mode=gray");
            throw new InvalidOperationException("gray failure");
        }

        if (request.Value == "slow")
        {
            evidence.Add($"profile-start|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
            await Task.Delay(TimeSpan.FromMilliseconds(700), cancellationToken);
        }

        evidence.Add($"profile-request|rid={evidence.Rid}|marker={request.Marker}|value={request.Value}");
        return new ProfileReply($"profile:{request.Value}", evidence.Rid, request.Marker);
    }
}

internal sealed class ProfileCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<ProfileCommand>
{
    public ValueTask HandleAsync(
        ProfileCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"profile-command|rid={evidence.Rid}|marker={command.Marker}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence, FaultState fault)
    : IZLinkMessageFlowObserver
{
    public ValueTask OnMessageFlowAsync(
        ZLinkMessageFlowEvent flow,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (flow.Outcome != ZLinkMessageFlowOutcome.Error)
        {
            return ValueTask.CompletedTask;
        }

        evidence.Add(
            "dispatch-error"
            + $"|surface={flow.Surface}"
            + $"|kind={flow.MessageKind}"
            + $"|reason={flow.ErrorReason}"
            + $"|action={flow.ErrorAction}"
            + $"|packet={flow.PacketName ?? "<null>"}"
            + $"|channel={flow.ChannelName ?? "<null>"}");
        if (fault.Mode == "observer-throws")
        {
            throw new InvalidOperationException("dispatch observer failure");
        }

        return ValueTask.CompletedTask;
    }
}

internal sealed class FaultState
{
    public string Mode { get; set; } = "none";
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

    public void Clear()
    {
        while (_entries.TryDequeue(out _))
        {
        }

        if (!string.IsNullOrWhiteSpace(_filePath))
        {
            lock (_fileGate)
            {
                File.WriteAllText(_filePath, string.Empty);
            }
        }
    }
}

internal sealed record ServerOptions(
    string Role,
    string Rid,
    string HttpUrl,
    string LogDir,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? EvidenceFile,
    int Weight)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
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

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string? Get(string name) => values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        return new ServerOptions(
            Role: defaultRole,
            Rid: Get("--rid") ?? "node",
            HttpUrl: Get("--http-url") ?? "http://127.0.0.1:0",
            LogDir: Get("--log-dir") ?? "logs",
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ChannelEndpoint: Get("--channel-endpoint"),
            EvidenceFile: Get("--evidence-file"),
            Weight: int.TryParse(Get("--weight"), out var weight) ? weight : 100);
    }
}
