using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using YieldDispatch.Shared;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Actors;

namespace YieldDispatch.Server.Session;

internal static class SessionHostFactory
{
    public static WebApplication Create(string[] args)
    {
        var options = SessionOptions.Parse(args);
        Directory.CreateDirectory(options.LogDir);

        var builder = WebApplication.CreateBuilder(args);
        builder.Logging.ClearProviders();
        builder.Logging.AddSimpleConsole(console =>
        {
            console.SingleLine = true;
            console.TimestampFormat = "HH:mm:ss.fff ";
        });
        builder.WebHost.UseUrls(options.HttpUrl);
        builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
        builder.Services.AddSingleton(new NodeOptions(options.Rid));
        builder.Services.AddZLinkFramework(framework =>
        {
            framework.AddHandlersFromAssemblyOf(typeof(Program));
            framework.ConfigureDispatch()
                .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
                .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
                .TraceLabel(options.Rid);
            framework.UseDiscovery().AddRegistryEndpoint(options.RegistryRouterEndpoint);
            framework.AddRouteMesh(YieldDispatchNames.ControlChannel)
                .EnableServer(options.ControlEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
            framework.AddSpotMesh(YieldDispatchNames.SpotChannel)
                .UseRegistrySpotResolver()
                .EnableRouter(options.SpotRouterEndpoint)
                .SetRoutingId(RoutingId.From(options.Rid))
                .AddEntrySpot<SessionYieldEntrySpot>()
                .AddActorFactory<SessionYieldActorFactory>(YieldDispatchNames.ActorType);
            framework.AddStreamNode(YieldDispatchNames.StreamNode)
                .Bind(options.StreamEndpoint)
                .RegisterSession<YieldSession>();
        });

        var app = builder.Build();
        app.MapGet("/health", () => Results.Ok(new { status = "ready", role = "session", options.Rid }));
        app.MapGet("/evidence", (EvidenceStore evidence) => Results.Ok(evidence.Snapshot()));
        return app;
    }
}

internal sealed class SessionYieldEntrySpot(IZLinkEntrySpotContext context) : IZLinkEntrySpot<SessionYieldActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;
}

internal sealed class SessionYieldActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new SessionYieldActor(actorId, context));
    }
}

internal sealed class SessionYieldActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}

internal sealed class YieldSession(
    IZLinkSessionContext context,
    IZLinkRouteClient routes,
    EvidenceStore evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-connected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-disconnected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-error|rid={evidence.Rid}|session={Context.SessionId}|error={error.Error}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZLinkSessionDispatchContext dispatch,
        ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        switch (dispatch.PacketName)
        {
            case "BindYieldActorsReq":
            {
                var request = payload.Decode<BindYieldActorsReq>();
                evidence.Add($"session-bind-actors|rid={evidence.Rid}|session={Context.SessionId}|spot={request.SpotRid}");
                var result = await RequestPlayControlWithRetryAsync<BindYieldActorsReply>(
                    routes,
                    request,
                    "BindYieldActorsReq",
                    cancellationToken);
                foreach (var actor in result.Actors)
                {
                    await Context.Actors.BindAsync(
                        new ActorRef(
                            RoutingId.From(actor.NodeRid),
                            actor.ActorId,
                            actor.Generation),
                        cancellationToken);
                    evidence.Add(
                        $"session-bound-actor|rid={evidence.Rid}|session={Context.SessionId}"
                        + $"|actor={actor.ActorId}|node={actor.NodeRid}");
                }

                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldTrackAReq":
            {
                var request = payload.Decode<YieldTrackAReq>();
                evidence.Add($"session-track-a|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}");
                var result = await RequestPlayControlWithRetryAsync<YieldScenarioResult>(
                    routes,
                    request,
                    "YieldTrackAReq",
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldTimeoutScenarioReq":
            {
                var request = payload.Decode<YieldTimeoutScenarioReq>();
                evidence.Add($"session-timeout|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}");
                var result = await RequestPlayControlWithRetryAsync<YieldScenarioResult>(
                    routes,
                    request,
                    "YieldTimeoutScenarioReq",
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldTimerScenarioReq":
            {
                var request = payload.Decode<YieldTimerScenarioReq>();
                evidence.Add($"session-timer|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}");
                var result = await RequestPlayControlWithRetryAsync<YieldScenarioResult>(
                    routes,
                    request,
                    "YieldTimerScenarioReq",
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldRemoteScenarioReq":
            {
                var request = payload.Decode<YieldRemoteScenarioReq>();
                evidence.Add(
                    $"session-remote|rid={evidence.Rid}|session={Context.SessionId}"
                    + $"|request={request.RequestId}|target={request.TargetNodeRid}");
                var result = await RequestPlayControlWithRetryAsync<YieldScenarioResult>(
                    routes,
                    request,
                    "YieldRemoteScenarioReq",
                    RoutingId.From("play-a"),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldRouteBridgeScenarioReq":
            {
                var request = payload.Decode<YieldRouteBridgeScenarioReq>();
                evidence.Add(
                    $"session-route-bridge|rid={evidence.Rid}|session={Context.SessionId}"
                    + $"|request={request.RequestId}|target={request.TargetNodeRid}");
                var result = await RequestPlayControlWithRetryAsync<YieldScenarioResult>(
                    routes,
                    request,
                    "YieldRouteBridgeScenarioReq",
                    RoutingId.From(request.TargetNodeRid),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldCancellationScenarioReq":
            {
                var request = payload.Decode<YieldCancellationScenarioReq>();
                evidence.Add($"session-cancel|rid={evidence.Rid}|session={Context.SessionId}|request={request.RequestId}");
                var result = await RequestPlayControlWithRetryAsync<YieldScenarioResult>(
                    routes,
                    request,
                    "YieldCancellationScenarioReq",
                    RoutingId.From("play-a"),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            case "YieldEvidenceReq":
            {
                var request = payload.Decode<YieldEvidenceReq>();
                var result = await RequestPlayControlWithRetryAsync<YieldEvidenceReply>(
                    routes,
                    request,
                    "YieldEvidenceReq",
                    RoutingId.From("play-a"),
                    cancellationToken);
                await Context.Client.Reply(result).Async();
                return;
            }
            default:
            {
                var actorId = dispatch.Metadata.Find(YieldDispatchNames.ActorIdMetadata);
                var actor = string.IsNullOrWhiteSpace(actorId)
                    ? RequireSingleBoundActor()
                    : Context.Actors.Find(actorId)
                      ?? throw new InvalidOperationException($"Actor route not found: {actorId}");
                await actor.RelayAsync(payload, cancellationToken);
                return;
            }
        }
    }

    private IZLinkSessionActor RequireSingleBoundActor()
    {
        return Context.Actors.Bound.Count switch
        {
            1 => Context.Actors.Bound.Single(),
            0 => throw new InvalidOperationException("No actor is bound."),
            _ => throw new InvalidOperationException("actor-id metadata is required.")
        };
    }

    private static async Task<TReply> RequestPlayControlWithRetryAsync<TReply>(
        IZLinkRouteClient routes,
        object request,
        string packetName,
        CancellationToken cancellationToken)
        => await RequestPlayControlWithRetryAsync<TReply>(
            routes,
            request,
            packetName,
            RoutingId.From("play-a"),
            cancellationToken);

    private static async Task<TReply> RequestPlayControlWithRetryAsync<TReply>(
        IZLinkRouteClient routes,
        object request,
        string packetName,
        RoutingId target,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(20);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                return await routes.Request(
                        YieldDispatchNames.ControlChannel,
                        target,
                        request)
                    .PacketName(packetName)
                    .Timeout(TimeSpan.FromSeconds(5))
                    .Async<TReply>(cancellationToken);
            }
            catch (Exception ex) when (
                ex is TimeoutException or ZlinkRequestException or ZlinkSubmitException
                || ex is ZLinkFrameworkException { InnerException: ZlinkRequestException or ZlinkSubmitException })
            {
                last = ex;
                await Task.Delay(100, cancellationToken);
            }
        }

        throw new TimeoutException($"Timed out requesting play control packet '{packetName}'.", last);
    }

}

internal sealed record NodeOptions(string Rid);

internal sealed class EvidenceStore
{
    private readonly object _gate = new();
    private readonly List<string> _entries = [];
    private readonly string? _filePath;

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
    }

    public string Rid { get; }

    public void Add(string entry)
    {
        lock (_gate)
        {
            _entries.Add(entry);
            if (!string.IsNullOrWhiteSpace(_filePath))
            {
                File.AppendAllText(_filePath, entry + Environment.NewLine);
            }
        }
    }

    public string[] Snapshot()
    {
        lock (_gate)
        {
            return _entries.ToArray();
        }
    }
}

internal sealed record SessionOptions(
    string Rid,
    string HttpUrl,
    string RegistryRouterEndpoint,
    string ControlEndpoint,
    string PlayControlEndpoint,
    string SpotRouterEndpoint,
    string StreamEndpoint,
    string LogDir)
{
    public string EvidenceFile => Path.Combine(LogDir, $"{Rid}.evidence.log");

    public static SessionOptions Parse(string[] args)
    {
        var values = Cli.Parse(args);
        return new SessionOptions(
            Cli.Required(values, "rid"),
            Cli.Required(values, "http-url"),
            Cli.Required(values, "registry-router-endpoint"),
            Cli.Required(values, "control-endpoint"),
            Cli.Required(values, "play-control-endpoint"),
            Cli.Required(values, "spot-router-endpoint"),
            Cli.Required(values, "stream-endpoint"),
            Cli.Required(values, "log-dir"));
    }
}

internal static class Cli
{
    public static Dictionary<string, string> Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {args[i]}.");
            }

            values[args[i][2..]] = args[++i];
        }

        return values;
    }

    public static string Required(Dictionary<string, string> values, string key)
        => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
}
