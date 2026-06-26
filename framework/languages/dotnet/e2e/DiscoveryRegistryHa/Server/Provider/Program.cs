using System.Collections.Concurrent;
using DiscoveryRegistryHa.Shared;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Registry;

var options = ServerOptions.Parse(args, defaultRole: "provider");
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
        registry.RegistryId = options.RegistryId;
        registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
        registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
        registry.HeartbeatInterval = TimeSpan.FromMilliseconds(250);
        registry.HeartbeatTimeout = TimeSpan.FromSeconds(2);
        registry.BroadcastInterval = TimeSpan.FromMilliseconds(250);
        foreach (var peer in options.PeerPubEndpoints)
        {
            registry.AddPeer(peer);
        }
    });
}
else if (options.Role == "provider" || options.Role == "embedded")
{
    if (options.Role == "embedded")
    {
        builder.Services.AddZLinkRegistry(registry =>
        {
            registry.RegistryId = options.RegistryId;
            registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
            registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
            registry.HeartbeatInterval = TimeSpan.FromMilliseconds(250);
            registry.HeartbeatTimeout = TimeSpan.FromSeconds(2);
            registry.BroadcastInterval = TimeSpan.FromMilliseconds(250);
            foreach (var peer in options.PeerPubEndpoints)
            {
                registry.AddPeer(peer);
            }
        });
    }

    builder.Services.AddZLinkFramework(framework =>
    {
        if (options.Role == "embedded" && options.DiscoveryEndpoints.Count == 0)
        {
            framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        }

        foreach (var endpoint in options.DiscoveryEndpoints)
        {
            framework.UseDiscovery().AddRegistryEndpoint(endpoint);
        }

        framework.ConfigureDispatch()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceLabel(options.Rid);

        var channel = framework.AddClientServerChannel(DiscoveryRegistryHaNames.Channel)
            .EnableServer(Require(options.ChannelEndpoint, "--channel-endpoint"))
            .SetRoutingId(RoutingId.From(options.Rid));
        channel.AddRequestHandler<ProfileRequestHandler, ProfileRequest, ProfileReply>("ProfileRequest");
    });
}
else
{
    throw new InvalidOperationException($"Unsupported role '{options.Role}'.");
}

var app = builder.Build();
app.MapGet("/health", () => Results.Ok(new { status = "ready", options.Role, options.Rid }));
app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
app.MapGet("/registry/status", async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
    Results.Ok(await query.StatusAsync(cancellationToken)));
app.MapGet("/registry/topology", async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
    Results.Ok(await query.TopologyAsync(
        new ZLinkRegistryTopologyFilter(ChannelName: DiscoveryRegistryHaNames.Channel),
        cancellationToken)));
app.MapGet("/registry/members", async ([FromServices] IZLinkRegistryQuery query, CancellationToken cancellationToken) =>
    Results.Ok(await query.MemberPeersAsync(DiscoveryRegistryHaNames.Channel, cancellationToken)));
app.MapPost("/shutdown", (IHostApplicationLifetime lifetime) =>
{
    lifetime.StopApplication();
    return Results.Ok(new { status = "stopping" });
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
    uint RegistryId,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    IReadOnlyList<string> PeerPubEndpoints,
    IReadOnlyList<string> DiscoveryEndpoints)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var peers = new List<string>();
        var discovery = new List<string>();
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
            if (string.Equals(key, "--peer-pub-endpoint", StringComparison.OrdinalIgnoreCase))
            {
                peers.Add(value);
            }
            else if (string.Equals(key, "--discovery-endpoint", StringComparison.OrdinalIgnoreCase))
            {
                discovery.Add(value);
            }
            else
            {
                values[key] = value;
            }
        }

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var value)
            ? value
            : fallback;

        uint GetUInt(string name, uint fallback = 0) => values.TryGetValue(name, out var value)
            ? uint.Parse(value)
            : fallback;

        return new ServerOptions(
            Role: defaultRole,
            HttpUrl: Get("--http-url", "http://127.0.0.1:0"),
            LogDir: Get("--log-dir", "logs"),
            EvidenceFile: Get("--evidence-file"),
            Rid: Get("--rid", Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node"),
            RegistryId: GetUInt("--registry-id", 1),
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ChannelEndpoint: Get("--channel-endpoint"),
            PeerPubEndpoints: peers,
            DiscoveryEndpoints: discovery);
    }
}
