using System.Collections.Concurrent;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Http;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using SpotService.Shared;
using Systems.Zlink;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.Framework;
using Zlink.Framework.AspNetCore;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Dispatch;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;

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
builder.Services.AddSingleton(new EvidenceStore(options.Rid, options.EvidenceFile));
builder.Services.AddSingleton(new NodeOptions(options.Rid));

if (options.Role == "registry")
{
    builder.Services.AddZLinkRegistry(registry =>
    {
        registry.PubEndpoint = Require(options.RegistryPubEndpoint, "--registry-pub-endpoint");
        registry.RouterEndpoint = Require(options.RegistryRouterEndpoint, "--registry-router-endpoint");
    });
}
else if (options.Role == "play")
{
    builder.Services.AddSingleton<IZLinkMessageDispatchErrorObserver, EvidenceDispatchErrorObserver>();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.AddHandlersFromAssemblyOf(typeof(Program));
        framework.ConfigureDispatch()
            .SetMessageDispatchErrorObserver<EvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceNodeId(options.Rid);
        framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        framework.AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType);
        framework.AddRouteMeshChannel(SpotServiceNames.ControlChannel)
            .EnableServer(Require(options.ControlEndpoint, "--control-endpoint"))
            .EnableClient()
            .SetRoutingId(RoutingId.From(options.Rid))
            .AddHandlerGroup("play");
        if (!string.IsNullOrWhiteSpace(options.ExternalSpotEndpoint))
        {
            framework.AddRouteMeshChannel(SpotServiceNames.ExternalSpotChannel)
                .EnableServer(options.ExternalSpotEndpoint)
                .EnableClient()
                .SetRoutingId(RoutingId.From(options.Rid));
        }

        var spot = framework.AddSpotMesh(SpotServiceNames.SpotChannel)
            .UseRegistrySpotResolver()
            .AddNode(SpotServiceNames.PlaySpotNode)
            .EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
            .SetRouterRoutingId(RoutingId.From(options.Rid))
            .EnablePubSub(Require(options.SpotPubEndpoint, "--spot-pub-endpoint"))
            .SetPubSubRoutingId(RoutingId.From(options.Rid))
            .AcceptSpotRoutesFromChannel(SpotServiceNames.ControlChannel)
            .AddEntrySpot<ScenarioEntrySpot>()
            .AddSpotFactory<ScenarioUserSpot>();
        if (!string.IsNullOrWhiteSpace(options.ExternalSpotEndpoint))
        {
            spot.AcceptSpotRoutesFromChannel(SpotServiceNames.ExternalSpotChannel);
        }
    });
}
else if (options.Role == "session")
{
    builder.Services.AddSingleton<IZLinkMessageDispatchErrorObserver, EvidenceDispatchErrorObserver>();
    builder.Services.AddZLinkFramework(framework =>
    {
        framework.AddHandlersFromAssemblyOf(typeof(Program));
        framework.ConfigureDispatch()
            .SetMessageDispatchErrorObserver<EvidenceDispatchErrorObserver>()
            .MessageFlow(ZLinkMessageFlowLogMode.KeyTransitions)
            .TraceLogFile(Path.Combine(options.LogDir, $"{options.Rid}-flow.log"))
            .TraceNodeId(options.Rid);
        framework.UseDiscovery().AddRegistryEndpoint(Require(options.RegistryRouterEndpoint, "--registry-router-endpoint"));
        framework.AddRouteMeshChannel(SpotServiceNames.ControlChannel)
            .EnableServer(Require(options.ControlEndpoint, "--control-endpoint"))
            .EnableClient()
            .SetRoutingId(RoutingId.From(options.Rid));
        framework.AddSpotMesh(SpotServiceNames.SpotChannel)
            .AddNode(SpotServiceNames.SessionSpotNode)
            .EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
            .SetRouterRoutingId(RoutingId.From(options.Rid));
        framework.AddStreamNode(SpotServiceNames.StreamNode)
            .AttachActorGateway(SpotServiceNames.SessionSpotNode)
            .Bind(Require(options.StreamEndpoint, "--stream-endpoint"))
            .RegisterSession<ScenarioSession>();
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
await app.RunAsync();

static string Require(string? value, string optionName)
    => string.IsNullOrWhiteSpace(value)
        ? throw new InvalidOperationException($"{optionName} is required.")
        : value;

[ZLinkHandlerGroup("play")]
internal sealed class EnsureActorHandler(
    IZLinkActorManager actors,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<EnsureActorReq, EnsureActorReply>
{
    public async ValueTask<EnsureActorReply> HandleAsync(
        EnsureActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            cancellationToken);
        if (actor is ScenarioActor scenarioActor)
        {
            scenarioActor.DisplayName = request.DisplayName;
        }

        using var joinRequest = Message.From(ReadOnlySpan<byte>.Empty);
        var joined = await actor.Context.JoinEntrySpot(RoutingId.From(node.Rid), joinRequest)
            .Async(cancellationToken);
        evidence.Add($"ensure-actor|rid={node.Rid}|actor={request.ActorId}");
        return new EnsureActorReply(
            joined.Actor.ActorId,
            joined.Actor.NodeRid.ToString(),
            joined.Actor.Generation);
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class ControlPingHandler(NodeOptions node)
    : IZLinkRouteRequestHandler<ControlPingReq, ControlPingReply>
{
    public ValueTask<ControlPingReply> HandleAsync(
        ControlPingReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(new ControlPingReply(request.Value, node.Rid));
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class CreateSpotHandler(
    IZLinkSpotManager spots,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<CreateSpotReq, CreateSpotReply>
{
    public async ValueTask<CreateSpotReply> HandleAsync(
        CreateSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var result = await spots.GetOrCreateAsync<ScenarioUserSpot>(
            RoutingId.From(request.SpotRid),
            cancellationToken);
        evidence.Add($"create-spot|rid={node.Rid}|spot={result.SpotRid}|state={result.State}");
        return new CreateSpotReply(result.SpotRid.ToString(), node.Rid, result.State.ToString());
    }
}

internal sealed class ScenarioActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new ScenarioActor(actorId, context));
    }
}

internal sealed class ScenarioActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public string DisplayName { get; set; } = actorId;

    public int Seen { get; set; }

    public IZLinkActorContext Context { get; } = context;
}

internal sealed class ScenarioEntrySpot(
    IZLinkEntrySpotContext context,
    EvidenceStore evidence) : IZLinkEntrySpot<ScenarioActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public ValueTask OnCreateActorAsync(
        ScenarioActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"entry-created|rid={evidence.Rid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        ScenarioActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        _ = actor;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(Message.From(request)));
    }

    public ValueTask OnJoinedActorAsync(
        ScenarioActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"entry-joined|rid={evidence.Rid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(
        ScenarioActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"entry-left|rid={evidence.Rid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectActorAsync(
        ScenarioActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"entry-disconnected|rid={evidence.Rid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class ScenarioUserSpot(
    IZLinkSpotContext context,
    EvidenceStore evidence) : IZLinkSpot<ScenarioActor>
{
    private int _value;

    public IZLinkSpotContext Context { get; } = context;

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-created|rid={evidence.Rid}|spot={Context.SpotRid}");
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-initialize|rid={evidence.Rid}|spot={Context.SpotRid}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnClosingAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-closing|rid={evidence.Rid}|spot={Context.SpotRid}");
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        ScenarioActor actor,
        Message request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-joined|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(Message.From(request)));
    }

    public ValueTask OnLeaveActorAsync(ScenarioActor actor, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-left|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnDisconnectActorAsync(ScenarioActor actor, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-disconnected|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public int Add(int delta)
    {
        _value += delta;
        return _value;
    }
}

[ZLinkSpotRequestHandler("StateReq")]
internal sealed class StateReqHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, StateReq, StateReply>
{
    public ValueTask<StateReply> HandleAsync(
        ScenarioUserSpot spot,
        StateReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var delta = string.Equals(request.Operation, "add", StringComparison.Ordinal) ? request.Delta : 0;
        var value = spot.Add(delta);
        evidence.Add($"spot-state-request|rid={evidence.Rid}|spot={spot.Context.SpotRid}|value={value}");
        return ValueTask.FromResult(new StateReply(
            spot.Context.SpotRid.ToString(),
            spot.Context.NodeRid.ToString(),
            value));
    }
}

[ZLinkSpotPacketHandler("StateCommand")]
internal sealed class StateCommandHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, StateCommand>
{
    public ValueTask HandleAsync(
        ScenarioUserSpot spot,
        StateCommand message,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-state-command|rid={evidence.Rid}|spot={spot.Context.SpotRid}|marker={message.Marker}");
        return ValueTask.CompletedTask;
    }
}

[ZLinkSpotActorRequestHandler("ActorPingReq")]
internal sealed class EntryActorPingHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPingReq, ActorPingReply>
{
    public ValueTask<ActorPingReply> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Seen++;
        return ValueTask.FromResult(new ActorPingReply(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("UserActorPingReq")]
internal sealed class UserActorPingHandler
    : IZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPingReq, ActorPingReply>
{
    public ValueTask<ActorPingReply> HandleAsync(
        ScenarioUserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.Seen++;
        return ValueTask.FromResult(new ActorPingReply(
            actor.ActorId,
            spot.Context.NodeRid.ToString(),
            spot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("ActorPushReq")]
internal sealed class ActorPushHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ActorPushReq, ActorPingReply>
{
    public async ValueTask<ActorPingReply> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushReq request,
        CancellationToken cancellationToken)
    {
        actor.Seen++;
        await actor.Context.BoundSession.Send(new ActorPushNotify(actor.ActorId, request.Value, actor.Seen))
            .PacketName("ActorPushNotify")
            .Async();
        return new ActorPingReply(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen);
    }
}

internal sealed class ScenarioSession(
    IZLinkSessionContext context,
    IZLinkSessionPacketDispatcher<IZLinkSessionContext> handlers,
    EvidenceStore evidence) : IZLinkSession
{
    public IZLinkSessionContext Context { get; } = context;

    public ValueTask OnConnectedAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-connected|rid={evidence.Rid}|session={Context.SessionId}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        foreach (var actor in Context.Actors.Bound.Take(1))
        {
            await actor.NotifyDisconnectedAsync(cancellationToken);
        }

        evidence.Add($"session-disconnected|rid={evidence.Rid}|session={Context.SessionId}");
    }

    public ValueTask OnErrorAsync(ZLinkStreamError error, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"session-error|rid={evidence.Rid}|error={error}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask OnDispatchAsync(
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(Context, header, payload, cancellationToken))
        {
            return;
        }

        var actorId = header.Metadata.Get(SpotServiceNames.ActorIdMetadata);
        var actor = string.IsNullOrWhiteSpace(actorId)
            ? RequireSingleBoundActor()
            : Context.Actors.Find(actorId)
              ?? throw new InvalidOperationException($"Actor route not found: {actorId}");
        await actor.RelayAsync(header, payload, cancellationToken);
    }

    private IZLinkSessionActor RequireSingleBoundActor()
    {
        return Context.Actors.Bound.Count switch
        {
            1 => Context.Actors.Bound.Single(),
            0 => throw new InvalidOperationException("No actor is bound."),
            _ => throw new InvalidOperationException("ActorRouteNotFound: actor-id metadata is required.")
        };
    }
}

internal sealed class AuthSessionHandler(
    IZLinkRouteClient routes)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "AuthReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var request = payload.Decode<AuthReq>();
        var ensured = await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(request.NodeRid),
                new EnsureActorReq(request.ActorId, request.DisplayName, request.NodeRid))
            .PacketName("EnsureActorReq")
            .Async<EnsureActorReply>(cancellationToken);
        await context.Actors.BindAsync(
            new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
            cancellationToken);
        await context.Client.Reply(new AuthReply(ensured.ActorId, ensured.NodeRid)).Async();
    }
}

internal sealed class MultiBindSessionHandler(
    IZLinkRouteClient routes)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "MultiBindReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZlinkStreamHeader header,
        Message payload,
        CancellationToken cancellationToken)
    {
        _ = header;
        var request = payload.Decode<MultiBindReq>();
        foreach (var actorId in new[] { request.FirstActorId, request.SecondActorId })
        {
            var ensured = await routes.Request(
                    SpotServiceNames.ControlChannel,
                    RoutingId.From(request.NodeRid),
                    new EnsureActorReq(actorId, actorId, request.NodeRid))
                .PacketName("EnsureActorReq")
                .Async<EnsureActorReply>(cancellationToken);
            await context.Actors.BindAsync(
                new ActorRef(RoutingId.From(ensured.NodeRid), ensured.ActorId, ensured.Generation),
                cancellationToken);
        }

        await context.Client.Reply(new MultiBindReply(context.Actors.Bound.Count)).Async();
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
            + $"|reason={error.Reason}"
            + $"|action={error.Action}"
            + $"|packet={error.PacketName ?? "<null>"}");
        return ValueTask.CompletedTask;
    }
}

internal sealed class EvidenceStore
{
    private readonly ConcurrentQueue<string> _entries = new();
    private readonly object _fileGate = new();
    private readonly string? _filePath;

    public EvidenceStore(string rid, string? filePath)
    {
        Rid = rid;
        _filePath = filePath;
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

internal sealed record NodeOptions(string Rid);

internal sealed record ServerOptions(
    string Role,
    string Rid,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ControlEndpoint,
    string? SpotRouterEndpoint,
    string? SpotPubEndpoint,
    string? ExternalSpotEndpoint,
    string? StreamEndpoint)
{
    public static ServerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
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

            values[key[2..]] = args[++i];
        }

        string Required(string key) => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");

        return new ServerOptions(
            Required("role"),
            Required("rid"),
            Required("http-url"),
            values.GetValueOrDefault("log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-spot-e2e")),
            values.GetValueOrDefault("evidence-file"),
            values.GetValueOrDefault("registry-pub-endpoint"),
            values.GetValueOrDefault("registry-router-endpoint"),
            values.GetValueOrDefault("control-endpoint"),
            values.GetValueOrDefault("spot-router-endpoint"),
            values.GetValueOrDefault("spot-pub-endpoint"),
            values.GetValueOrDefault("external-spot-endpoint"),
            values.GetValueOrDefault("stream-endpoint"));
    }
}
