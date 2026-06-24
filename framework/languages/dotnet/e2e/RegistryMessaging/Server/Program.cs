using System.Collections.Concurrent;
using System.Security.Cryptography;
using System.Text;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using RegistryMessaging.Shared;
using Systems.Zlink;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Registry;

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

if (options.Role == "registry")
{
    builder.Services.AddZLinkRegistry(registry =>
    {
        registry.PubEndpoint = options.RegistryPubEndpoint
            ?? throw new InvalidOperationException("--registry-pub-endpoint is required.");
        registry.RouterEndpoint = options.RegistryRouterEndpoint
            ?? throw new InvalidOperationException("--registry-router-endpoint is required.");
    });
}
else if (options.Role == "provider")
{
    builder.Services.AddSingleton<IZLinkMessageDispatchErrorObserver, EvidenceDispatchErrorObserver>();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint
            ?? throw new InvalidOperationException("--registry-router-endpoint is required."));
        framework.ConfigureDispatch()
            .SetMessageDispatchErrorObserver<EvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceNodeId(options.Rid);

        if (!string.IsNullOrWhiteSpace(options.ChannelEndpoint))
        {
            var clientServer = framework.AddClientServerChannel("profile")
                .EnableServer(options.ChannelEndpoint);
            clientServer.ConfigureServerRouting().RoutingId = RoutingId.From(options.Rid);
            clientServer.ConfigureServerSocket().Weight = options.Weight;
            clientServer.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
            clientServer.AddRequestHandler<PayloadRequestHandler, PayloadRequest, PayloadReply>("PayloadRequest");
            clientServer.AddSendHandler<ProfileCommandHandler, ProfileCommand>("ProfileCommand");
        }

        if (!string.IsNullOrWhiteSpace(options.WorkflowEndpoint))
        {
            var workflow = framework.AddClientServerChannel("workflow")
                .EnableServer(options.WorkflowEndpoint);
            workflow.ConfigureServerRouting().RoutingId = RoutingId.From(options.Rid);
            workflow.ConfigureServerSocket().Weight = options.Weight;
            workflow.AddRequestHandler<WorkflowRequestHandler, WorkflowRequest, WorkflowReply>("WorkflowRequest");
        }

        if (!string.IsNullOrWhiteSpace(options.RouteEndpoint))
        {
            var route = framework.AddRouteMeshChannel("profile.route")
                .EnableServer(options.RouteEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid));
            foreach (var peer in options.RoutePeers)
            {
                route.EnableClient(peer);
            }

            route.AddRequestHandler<RoutePingHandler, ScenarioRoutePing, ScenarioRoutePong>("ScenarioRoutePing");
        }

        if (!string.IsNullOrWhiteSpace(options.DealerEndpoint))
        {
            var dealer = framework.AddDealerMeshChannel("profile.mesh")
                .EnableServer(options.DealerEndpoint);
            dealer.ConfigureSocket().Weight = options.Weight;
            foreach (var peer in options.DealerPeers)
            {
                dealer.EnableClient(peer);
            }

            dealer.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
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
await app.RunAsync();

internal sealed class ProfileRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<ProfileRequest, ProfileReply>
{
    public async ValueTask<ProfileReply> HandleAsync(
        ProfileRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        if (request.Value == "slow")
        {
            await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        }

        evidence.Add($"profile-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return new ProfileReply($"profile:{request.Value}", evidence.Rid);
    }
}

internal sealed class ProfileCommandHandler(EvidenceStore evidence)
    : IZLinkSendHandler<ProfileCommand>
{
    public async ValueTask HandleAsync(
        ProfileCommand command,
        ZLinkSendContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (command.CommandId.StartsWith("rm-c9-slow-", StringComparison.Ordinal))
        {
            await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
        }

        evidence.Add($"profile-command|rid={evidence.Rid}|command={command.CommandId}|packet={context.PacketName}");
    }
}

internal sealed class PayloadRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<PayloadRequest, PayloadReply>
{
    public ValueTask<PayloadReply> HandleAsync(
        PayloadRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var hash = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(request.Payload)));
        evidence.Add(
            $"payload-request|rid={evidence.Rid}|marker={request.Marker}"
            + $"|length={request.Payload.Length}|sha256={hash}|packet={context.PacketName}");
        return ValueTask.FromResult(new PayloadReply(request.Marker, request.Payload.Length, hash));
    }
}

internal sealed class WorkflowRequestHandler(EvidenceStore evidence)
    : IZLinkRequestHandler<WorkflowRequest, WorkflowReply>
{
    public ValueTask<WorkflowReply> HandleAsync(
        WorkflowRequest request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"workflow-request|rid={evidence.Rid}|value={request.Value}|packet={context.PacketName}");
        return ValueTask.FromResult(new WorkflowReply($"workflow:{request.Value}", evidence.Rid));
    }
}

internal sealed class RoutePingHandler(EvidenceStore evidence)
    : IZLinkRouteRequestHandler<ScenarioRoutePing, ScenarioRoutePong>
{
    public ValueTask<ScenarioRoutePong> HandleAsync(
        ScenarioRoutePing request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var source = context.SourceNodeRid.ToString();
        evidence.Add($"route-request|rid={evidence.Rid}|source={source}|value={request.Value}");
        return ValueTask.FromResult(new ScenarioRoutePong($"route:{request.Value}", evidence.Rid, source));
    }
}

internal sealed class EvidenceDispatchErrorObserver(EvidenceStore evidence)
    : IZLinkMessageDispatchErrorObserver
{
    public ValueTask OnDispatchErrorAsync(
        ZLinkMessageDispatchErrorEvent error,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            "dispatch-error"
            + $"|surface={error.Surface}"
            + $"|kind={error.MessageKind}"
            + $"|reason={error.Reason}"
            + $"|action={error.Action}"
            + $"|packet={error.PacketName ?? "<null>"}"
            + $"|channel={error.ChannelName ?? "<null>"}");
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
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    string? WorkflowEndpoint,
    string? RouteEndpoint,
    string? DealerEndpoint,
    int Weight,
    IReadOnlyList<string> RoutePeers,
    IReadOnlyList<string> DealerPeers)
{
    public static ServerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var routePeers = new List<string>();
        var dealerPeers = new List<string>();

        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            var value = args[++i];
            if (key == "--route-peer")
            {
                routePeers.Add(value);
            }
            else if (key == "--dealer-peer")
            {
                dealerPeers.Add(value);
            }
            else
            {
                values[key[2..]] = value;
            }
        }

        var rid = values.GetValueOrDefault("rid", "node");
        Environment.SetEnvironmentVariable("ZLINK_E2E_RID", rid);
        return new ServerOptions(
            values.GetValueOrDefault("role", "provider"),
            values.GetValueOrDefault("http-url", "http://127.0.0.1:0"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            values.GetValueOrDefault("evidence-file"),
            rid,
            values.GetValueOrDefault("registry-pub-endpoint"),
            values.GetValueOrDefault("registry-router-endpoint"),
            values.GetValueOrDefault("channel-endpoint"),
            values.GetValueOrDefault("workflow-endpoint"),
            values.GetValueOrDefault("route-endpoint"),
            values.GetValueOrDefault("dealer-endpoint"),
            int.TryParse(values.GetValueOrDefault("weight"), out var weight) ? weight : 100,
            routePeers,
            dealerPeers);
    }
}
