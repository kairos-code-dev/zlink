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
using Zlink.Framework.Contracts.Errors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
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
        if (!string.IsNullOrWhiteSpace(options.ExternalClientEndpoint))
        {
            framework.AddClientServerChannel(SpotServiceNames.ExternalClientChannel)
                .EnableClient(options.ExternalClientEndpoint);
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
            .AddSpotFactory<ScenarioUserSpot>()
            .AddSpotFactory<ScenarioAlternateSpot>();
        if (!string.IsNullOrWhiteSpace(options.ExternalSpotEndpoint))
        {
            spot.AcceptSpotRoutesFromChannel(SpotServiceNames.ExternalSpotChannel);
        }
        if (!string.IsNullOrWhiteSpace(options.ClientSpotPubEndpoint))
        {
            spot.ConnectPubSub(options.ClientSpotPubEndpoint);
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
        framework.AddActorFactory<ScenarioActorFactory>(SpotServiceNames.ActorType);
        framework.AddRouteMeshChannel(SpotServiceNames.ControlChannel)
            .EnableServer(Require(options.ControlEndpoint, "--control-endpoint"))
            .EnableClient()
            .SetRoutingId(RoutingId.From(options.Rid))
            .AddHandlerGroup("play");
        framework.AddSpotMesh(SpotServiceNames.SpotChannel)
            .UseRegistrySpotResolver()
            .AddNode(SpotServiceNames.SessionSpotNode)
            .EnableRouter(Require(options.SpotRouterEndpoint, "--spot-router-endpoint"))
            .SetRouterRoutingId(RoutingId.From(options.Rid))
            .AddEntrySpot<ScenarioEntrySpot>();
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

        var joined = await actor.Context.JoinEntrySpot(RoutingId.From(node.Rid), ZLinkMessage.Empty)
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

[ZLinkHandlerGroup("play")]
internal sealed class CloseSpotHandler(
    IZLinkSpotManager spots,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<CloseSpotReq, CloseSpotReply>
{
    public async ValueTask<CloseSpotReply> HandleAsync(
        CloseSpotReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var closed = await spots.CloseAsync(RoutingId.From(request.SpotRid), cancellationToken);
        evidence.Add($"close-spot|rid={evidence.Rid}|spot={request.SpotRid}|closed={closed}");
        return new CloseSpotReply(request.SpotRid, closed);
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class SpotTypeMismatchHandler(
    IZLinkSpotManager spots,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<SpotTypeMismatchReq, SpotTypeMismatchReply>
{
    public async ValueTask<SpotTypeMismatchReply> HandleAsync(
        SpotTypeMismatchReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var rid = RoutingId.From(request.SpotRid);
        var first = await spots.GetOrCreateAsync<ScenarioUserSpot>(rid, cancellationToken);
        try
        {
            await spots.GetOrCreateAsync<ScenarioAlternateSpot>(rid, cancellationToken);
        }
        catch (ZLinkFrameworkException ex) when (ex.Kind == ZLinkFrameworkErrorKind.SpotTypeMismatch)
        {
            evidence.Add($"spot-type-mismatch|rid={evidence.Rid}|spot={request.SpotRid}|kind={ex.Kind}");
            return new SpotTypeMismatchReply(request.SpotRid, true, ex.Kind.ToString(), first.State.ToString());
        }

        throw new InvalidOperationException("Expected SpotTypeMismatch for reused spot rid.");
    }
}

[ZLinkHandlerGroup("play")]
internal sealed class JoinUserSpotActorHandler(
    IZLinkActorManager actors,
    EvidenceStore evidence)
    : IZLinkRouteRequestHandler<JoinUserSpotActorReq, JoinUserSpotActorReply>
{
    public async ValueTask<JoinUserSpotActorReply> HandleAsync(
        JoinUserSpotActorReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            cancellationToken);
        var joined = await actor.Context.JoinSpot(RoutingId.From(request.SpotRid), ZLinkMessage.Empty)
            .Async(cancellationToken);
        evidence.Add(
            $"join-user-spot-actor|rid={evidence.Rid}|spot={request.SpotRid}"
            + $"|actor={request.ActorId}|accepted={joined.Accepted}");
        return new JoinUserSpotActorReply(
            request.SpotRid,
            joined.Actor.ActorId,
            joined.Accepted,
            joined.Actor.Generation);
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
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = actor;
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
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

    public void Configure()
    {
        Context.Handlers.AddSubscribe<SpotEventHandler>(SpotServiceNames.SpotEventTopic);
    }

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
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-joined|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
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

internal sealed class ScenarioAlternateSpot(
    IZLinkSpotContext context) : IZLinkSpot
{
    public IZLinkSpotContext Context { get; } = context;
}

internal sealed class ScenarioStage(ScenarioUserSpot spot)
{
    public StageProbeReply Apply(StageProbeReq request, EvidenceStore evidence)
    {
        var value = spot.Add(request.Delta);
        evidence.Add(
            $"stage-request|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|marker={request.Marker}|value={value}");
        return new StageProbeReply(
            spot.Context.SpotRid.ToString(),
            spot.Context.NodeRid.ToString(),
            value,
            request.Marker);
    }

    public async ValueTask StartTimerAsync(
        StageTimerStartCommand command,
        CancellationToken cancellationToken)
    {
        await spot.Context.AddTimer<StageTimerHandler>(
            command.Name,
            TimeSpan.FromMilliseconds(command.PeriodMs),
            cancellationToken: cancellationToken);
    }
}

[ZLinkSpotSubscriptionHandler(SpotServiceNames.SpotEventTopic)]
internal sealed class SpotEventHandler(EvidenceStore evidence)
    : IZLinkSpotSubscriptionHandler<ScenarioUserSpot, SpotEvent>
{
    public ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotEvent message,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-event|rid={evidence.Rid}|spot={spot.Context.SpotRid}|marker={message.Marker}");
        return ValueTask.CompletedTask;
    }
}

[ZLinkSpotRequestHandler("StageProbeReq")]
internal sealed class StageProbeHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, StageProbeReq, StageProbeReply>
{
    public ValueTask<StageProbeReply> HandleAsync(
        ScenarioUserSpot spot,
        StageProbeReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var stage = new ScenarioStage(spot);
        return ValueTask.FromResult(stage.Apply(request, evidence));
    }
}

[ZLinkSpotPacketHandler("StageTimerStartCommand")]
internal sealed class StageTimerStartHandler
    : IZLinkSpotPacketHandler<ScenarioUserSpot, StageTimerStartCommand>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        StageTimerStartCommand request,
        CancellationToken cancellationToken)
    {
        var stage = new ScenarioStage(spot);
        await stage.StartTimerAsync(request, cancellationToken);
    }
}

internal sealed class StageTimerHandler(EvidenceStore evidence)
    : IZLinkSpotTimerHandler<ScenarioUserSpot>
{
    public ValueTask HandleAsync(
        ScenarioUserSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"stage-timer|rid={evidence.Rid}|spot={spot.Context.SpotRid}|name={tick.Name}"
            + $"|delivery={tick.DeliveryIndex}");
        return ValueTask.CompletedTask;
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

[ZLinkSpotRequestHandler("WorkerStartReq")]
internal sealed class WorkerStartHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, WorkerStartReq, WorkerStartReply>
{
    public ValueTask<WorkerStartReply> HandleAsync(
        ScenarioUserSpot spot,
        WorkerStartReq request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"worker-start|rid={evidence.Rid}|spot={spot.Context.SpotRid}|marker={request.Marker}");
        spot.Context.RunWorker(ct =>
            {
                ct.ThrowIfCancellationRequested();
                Thread.Sleep(TimeSpan.FromMilliseconds(request.DelayMs));
                return request.Marker;
            })
            .Submit(
                (marker, ct) =>
                {
                    ct.ThrowIfCancellationRequested();
                    spot.Add(100);
                    evidence.Add($"worker-complete|rid={evidence.Rid}|spot={spot.Context.SpotRid}|marker={marker}");
                    return ValueTask.CompletedTask;
                },
                cancellationToken: cancellationToken);
        return ValueTask.FromResult(new WorkerStartReply(
            spot.Context.SpotRid.ToString(),
            spot.Context.NodeRid.ToString(),
            request.Marker));
    }
}

[ZLinkSpotPacketHandler("OverrunStartCommand")]
internal sealed class OverrunStartHandler
    : IZLinkSpotPacketHandler<ScenarioUserSpot, OverrunStartCommand>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        OverrunStartCommand request,
        CancellationToken cancellationToken)
    {
        var policy = Enum.Parse<ZLinkTimerOverrunPolicy>(request.Policy, ignoreCase: false);
        await spot.Context.AddTimer<OverrunTimerHandler>(
            request.Name,
            TimeSpan.FromMilliseconds(request.PeriodMs),
            new ZLinkTimerOptions
            {
                OverrunPolicy = policy,
                MaxCatchUpTicks = 2,
            },
            cancellationToken);
    }
}

internal sealed class OverrunTimerHandler(EvidenceStore evidence)
    : IZLinkSpotTimerHandler<ScenarioUserSpot>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        evidence.Add(
            $"timer-overrun|rid={evidence.Rid}|spot={spot.Context.SpotRid}|name={tick.Name}"
            + $"|delivery={tick.DeliveryIndex}|scheduled={tick.ScheduledIndex}|skipped={tick.SkippedTicks}");
        await Task.Delay(TimeSpan.FromMilliseconds(90), cancellationToken);
    }
}

[ZLinkSpotPacketHandler("TimerStartCommand")]
internal sealed class TimerStartHandler
    : IZLinkSpotPacketHandler<ScenarioUserSpot, TimerStartCommand>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        TimerStartCommand request,
        CancellationToken cancellationToken)
    {
        await spot.Context.AddTimer<BasicTimerHandler>(
            request.Name,
            TimeSpan.FromMilliseconds(request.PeriodMs),
            cancellationToken: cancellationToken);
    }
}

internal sealed class BasicTimerHandler(EvidenceStore evidence)
    : IZLinkSpotTimerHandler<ScenarioUserSpot>
{
    public ValueTask HandleAsync(
        ScenarioUserSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add(
            $"timer-basic|rid={evidence.Rid}|spot={spot.Context.SpotRid}|name={tick.Name}"
            + $"|delivery={tick.DeliveryIndex}");
        return ValueTask.CompletedTask;
    }
}

[ZLinkSpotPacketHandler("IdleCloseCommand")]
internal sealed class IdleCloseHandler
    : IZLinkSpotPacketHandler<ScenarioUserSpot, IdleCloseCommand>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        IdleCloseCommand request,
        CancellationToken cancellationToken)
    {
        await spot.Context.AddTimer<IdleCloseTimerHandler>(
            request.Name,
            TimeSpan.FromMilliseconds(request.PeriodMs),
            cancellationToken: cancellationToken);
    }
}

internal sealed class IdleCloseTimerHandler(EvidenceStore evidence)
    : IZLinkSpotTimerHandler<ScenarioUserSpot>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        ZLinkTimerTick tick,
        CancellationToken cancellationToken)
    {
        if (tick.DeliveryIndex > 1)
        {
            return;
        }

        var closed = await spot.Context.CloseAsync(cancellationToken);
        evidence.Add(
            $"timer-idle-close|rid={evidence.Rid}|spot={spot.Context.SpotRid}|name={tick.Name}|closed={closed}");
    }
}

[ZLinkSpotRequestHandler("SpotToSpotReq")]
internal sealed class SpotToSpotHandler(EvidenceStore evidence)
    : IZLinkSpotRequestHandler<ScenarioUserSpot, SpotToSpotReq, SpotToSpotReply>
{
    public async ValueTask<SpotToSpotReply> HandleAsync(
        ScenarioUserSpot spot,
        SpotToSpotReq request,
        CancellationToken cancellationToken)
    {
        var targetRid = RoutingId.From(request.TargetSpotRid);
        var reply = await spot.Context.Outbound
            .RequestToSpot(targetRid, new StateReq("add", 3))
            .PacketName("StateReq")
            .Async<StateReply>(cancellationToken);
        await spot.Context.Outbound
            .SendToSpot(targetRid, new StateCommand($"sm-c3-send-{request.Marker}"))
            .PacketName("StateCommand")
            .Async(cancellationToken);
        await spot.Context.Outbound
            .Publish(SpotServiceNames.SpotEventTopic, new SpotEvent($"sm-c3-publish-{request.Marker}"))
            .PacketName("SpotEvent")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-to-spot|rid={evidence.Rid}|source={spot.Context.SpotRid}"
            + $"|target={request.TargetSpotRid}|value={reply.Value}");
        return new SpotToSpotReply(
            spot.Context.SpotRid.ToString(),
            request.TargetSpotRid,
            reply.Value);
    }
}

[ZLinkSpotPacketHandler("SpotOutboundReq")]
internal sealed class SpotOutboundHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundReq>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotOutboundReq request,
        CancellationToken cancellationToken)
    {
        var echo = await spot.Context.Outbound
            .RequestToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelEchoReq(request.Marker))
            .PacketName("ChannelEchoReq")
            .Async<ChannelEchoReply>(cancellationToken);
        var notifyMarker = $"notify-{request.Marker}";
        await spot.Context.Outbound
            .SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify(notifyMarker))
            .PacketName("ChannelNotify")
            .Async(cancellationToken);
        await spot.Context.Outbound
            .Publish(
                SpotServiceNames.SpotEventTopic,
                new SpotEvent("sm-c2-publish"))
            .PacketName("SpotEvent")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-outbound|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|echo={echo.Value}|notify={notifyMarker}");
    }
}

[ZLinkSpotPacketHandler("SpotOutboundNegativeReq")]
internal sealed class SpotOutboundNegativeHandler(EvidenceStore evidence)
    : IZLinkSpotPacketHandler<ScenarioUserSpot, SpotOutboundNegativeReq>
{
    public async ValueTask HandleAsync(
        ScenarioUserSpot spot,
        SpotOutboundNegativeReq request,
        CancellationToken cancellationToken)
    {
        var requestFailed = false;
        try
        {
            await spot.Context.Outbound
                .RequestToChannel(
                    SpotServiceNames.ExternalClientChannel,
                    new ChannelEchoReq(request.Marker))
                .PacketName("MissingChannelReq")
                .Timeout(TimeSpan.FromSeconds(2))
                .Async<ChannelEchoReply>(cancellationToken);
        }
        catch
        {
            requestFailed = true;
        }

        await spot.Context.Outbound
            .SendToChannel(
                SpotServiceNames.ExternalClientChannel,
                new ChannelNotify($"missing-{request.Marker}"))
            .PacketName("MissingChannelSend")
            .Async(cancellationToken);
        evidence.Add(
            $"spot-outbound-negative|rid={evidence.Rid}|spot={spot.Context.SpotRid}"
            + $"|requestFailed={requestFailed}");
    }
}

[ZLinkSpotActorRequestHandler("ActorPingReq")]
internal sealed class EntryActorPingHandler(EvidenceStore evidence)
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
        evidence.Add(
            $"actor-ping|rid={entrySpot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={entrySpot.Context.SpotRid}|value={request.Value}|seen={actor.Seen}");
        return ValueTask.FromResult(new ActorPingReply(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("SlowActorPingReq")]
internal sealed class EntrySlowActorPingHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SlowActorPingReq, ActorPingReply>
{
    public async ValueTask<ActorPingReply> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        SlowActorPingReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        await Task.Delay(TimeSpan.FromMilliseconds(request.DelayMs), cancellationToken);
        actor.Seen++;
        evidence.Add(
            $"actor-slow-ping|rid={entrySpot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={entrySpot.Context.SpotRid}|value={request.Value}|seen={actor.Seen}");
        return new ActorPingReply(
            actor.ActorId,
            entrySpot.Context.NodeRid.ToString(),
            entrySpot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen);
    }
}

[ZLinkSpotActorRequestHandler("UserActorPingReq")]
internal sealed class UserActorPingHandler(EvidenceStore evidence)
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
        evidence.Add(
            $"actor-ping|rid={spot.Context.NodeRid}|actor={actor.ActorId}"
            + $"|spot={spot.Context.SpotRid}|value={request.Value}|seen={actor.Seen}");
        return ValueTask.FromResult(new ActorPingReply(
            actor.ActorId,
            spot.Context.NodeRid.ToString(),
            spot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("LeaveReq")]
internal sealed class UserActorLeaveHandler
    : IZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, LeaveReq, LeaveReply>
{
    public async ValueTask<LeaveReply> HandleAsync(
        ScenarioUserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        LeaveReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Leave request actor does not match dispatched actor.");
        }

        await spot.Context.leaveActor(actor, cancellationToken);
        return new LeaveReply(actor.ActorId, true);
    }
}

[ZLinkSpotActorRequestHandler("SnapshotReq")]
internal sealed class EntryActorSnapshotHandler
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, SnapshotReq, SnapshotReply>
{
    public ValueTask<SnapshotReply> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        SnapshotReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Snapshot request actor does not match dispatched actor.");
        }

        return ValueTask.FromResult(new SnapshotReply(actor.ActorId, actor.Seen));
    }
}

[ZLinkSpotActorRequestHandler("DestroyActorReq")]
internal sealed class EntryActorDestroyHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, DestroyActorReq, DestroyActorReply>
{
    public ValueTask<DestroyActorReply> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        DestroyActorReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        if (!string.Equals(request.ActorId, actor.ActorId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException("Destroy request actor does not match dispatched actor.");
        }

        _ = Task.Run(async () =>
        {
            try
            {
                await entrySpot.Context.DestroyActorAsync(actor);
                evidence.Add($"actor-destroyed|rid={evidence.Rid}|actor={actor.ActorId}");
            }
            catch (Exception ex)
            {
                evidence.Add(
                    $"actor-destroy-failed|rid={evidence.Rid}|actor={actor.ActorId}|error={ex.GetType().Name}");
            }
        });
        return ValueTask.FromResult(new DestroyActorReply(actor.ActorId, true));
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

[ZLinkSpotActorRequestHandler("UserActorPushReq")]
internal sealed class UserActorPushHandler
    : IZLinkSpotActorRequestHandler<ScenarioUserSpot, ScenarioActor, ActorPushReq, ActorPingReply>
{
    public async ValueTask<ActorPingReply> HandleAsync(
        ScenarioUserSpot spot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ActorPushReq request,
        CancellationToken cancellationToken)
    {
        _ = context;
        actor.Seen++;
        await actor.Context.BoundSession.Send(new ActorPushNotify(actor.ActorId, request.Value, actor.Seen))
            .PacketName("ActorPushNotify")
            .Async();
        return new ActorPingReply(
            actor.ActorId,
            spot.Context.NodeRid.ToString(),
            spot.Context.SpotRid.ToString(),
            request.Value,
            actor.Seen);
    }
}

[ZLinkSpotActorRequestHandler("ComplexActorReq")]
internal sealed class ComplexActorHandler(EvidenceStore evidence)
    : IZLinkEntrySpotActorRequestHandler<ScenarioEntrySpot, ScenarioActor, ComplexActorReq, ComplexActorReply>
{
    public ValueTask<ComplexActorReply> HandleAsync(
        ScenarioEntrySpot entrySpot,
        ScenarioActor actor,
        ZLinkSpotActorRequestContext context,
        ComplexActorReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        actor.DisplayName = request.DisplayName;
        evidence.Add(
            $"actor-complex|rid={evidence.Rid}|actor={actor.ActorId}|name={request.DisplayName}"
            + $"|level={request.Level}|tags={string.Join(",", request.Tags)}"
            + $"|attrs={string.Join(",", request.Attributes.OrderBy(static pair => pair.Key).Select(static pair => $"{pair.Key}:{pair.Value}"))}");
        return ValueTask.FromResult(new ComplexActorReply(
            actor.ActorId,
            request.DisplayName,
            request.Level,
            request.Tags,
            request.Attributes));
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
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        if (await handlers.TryHandleAsync(Context, dispatch, payload, cancellationToken))
        {
            return;
        }

        var actorId = dispatch.Metadata.Find(SpotServiceNames.ActorIdMetadata);
        var actor = string.IsNullOrWhiteSpace(actorId)
            ? RequireSingleBoundActor()
            : Context.Actors.Find(actorId)
              ?? throw new InvalidOperationException($"Actor route not found: {actorId}");
        await actor.RelayAsync(payload, cancellationToken);
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
    IZLinkRouteClient routes,
    IZLinkActorManager actors,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "AuthReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<AuthReq>();
        var ensured = string.Equals(request.NodeRid, node.Rid, StringComparison.Ordinal)
            ? await EnsureLocalActorAsync(actors, node, evidence, request, cancellationToken)
            : await routes.Request(
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

    private static async ValueTask<EnsureActorReply> EnsureLocalActorAsync(
        IZLinkActorManager actors,
        NodeOptions node,
        EvidenceStore evidence,
        AuthReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            cancellationToken);
        if (actor is ScenarioActor scenarioActor)
        {
            scenarioActor.DisplayName = request.DisplayName;
        }

        var joined = await actor.Context.JoinEntrySpot(RoutingId.From(node.Rid), ZLinkMessage.Empty)
            .Async(cancellationToken);
        evidence.Add($"ensure-actor|rid={node.Rid}|actor={request.ActorId}");
        return new EnsureActorReply(
            joined.Actor.ActorId,
            joined.Actor.NodeRid.ToString(),
            joined.Actor.Generation);
    }
}

internal sealed class MultiBindSessionHandler(
    IZLinkRouteClient routes)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "MultiBindReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
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

internal sealed class UserSpotAuthSessionHandler(
    IZLinkActorManager actors,
    IZLinkRouteClient routes,
    NodeOptions node,
    EvidenceStore evidence)
    : IZLinkSessionPacketHandler<IZLinkSessionContext>
{
    public string PacketName => "UserSpotAuthReq";

    public async ValueTask HandleAsync(
        IZLinkSessionContext context,
        ZLinkSessionDispatchContext dispatch,
        Zlink.Framework.Contracts.Messaging.ZLinkMessage payload,
        CancellationToken cancellationToken)
    {
        _ = dispatch;
        var request = payload.Decode<UserSpotAuthReq>();
        var joined = string.Equals(request.NodeRid, node.Rid, StringComparison.Ordinal)
            ? await JoinLocalUserSpotAsync(actors, evidence, request, cancellationToken)
            : await JoinRemoteUserSpotAsync(routes, request, cancellationToken);

        EnsureAccepted(joined);
        await context.Actors.BindAsync(
            new ActorRef(RoutingId.From(request.NodeRid), joined.ActorId, joined.Generation),
            cancellationToken);
        await context.Client.Reply(new AuthReply(joined.ActorId, request.NodeRid)).Async();
    }

    private static async ValueTask<JoinUserSpotActorReply> JoinLocalUserSpotAsync(
        IZLinkActorManager actors,
        EvidenceStore evidence,
        UserSpotAuthReq request,
        CancellationToken cancellationToken)
    {
        var actor = await actors.GetOrCreateAsync(
            request.ActorId,
            SpotServiceNames.ActorType,
            cancellationToken);
        if (actor is ScenarioActor scenarioActor)
        {
            scenarioActor.DisplayName = request.DisplayName;
        }

        var joined = await actor.Context.JoinSpot(RoutingId.From(request.SpotRid), ZLinkMessage.Empty)
            .Async(cancellationToken);
        evidence.Add(
            $"join-user-spot-actor|rid={evidence.Rid}|spot={request.SpotRid}"
            + $"|actor={request.ActorId}|accepted={joined.Accepted}");
        return new JoinUserSpotActorReply(
            request.SpotRid,
            joined.Actor.ActorId,
            joined.Accepted,
            joined.Actor.Generation);
    }

    private static async ValueTask<JoinUserSpotActorReply> JoinRemoteUserSpotAsync(
        IZLinkRouteClient routes,
        UserSpotAuthReq request,
        CancellationToken cancellationToken)
    {
        await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(request.NodeRid),
                new CreateSpotReq(request.SpotRid))
            .PacketName("CreateSpotReq")
            .Async<CreateSpotReply>(cancellationToken);
        return await routes.Request(
                SpotServiceNames.ControlChannel,
                RoutingId.From(request.NodeRid),
                new JoinUserSpotActorReq(request.SpotRid, request.ActorId))
            .PacketName("JoinUserSpotActorReq")
            .Async<JoinUserSpotActorReply>(cancellationToken);
    }

    private static void EnsureAccepted(JoinUserSpotActorReply joined)
    {
        if (!joined.Accepted)
        {
            throw new InvalidOperationException($"User spot actor join was rejected: {joined.ActorId}");
        }
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
    string? ExternalClientEndpoint,
    string? ExternalSpotEndpoint,
    string? ClientSpotPubEndpoint,
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
            values.GetValueOrDefault("external-client-endpoint"),
            values.GetValueOrDefault("external-spot-endpoint"),
            values.GetValueOrDefault("client-spot-pub-endpoint"),
            values.GetValueOrDefault("stream-endpoint"));
    }
}
