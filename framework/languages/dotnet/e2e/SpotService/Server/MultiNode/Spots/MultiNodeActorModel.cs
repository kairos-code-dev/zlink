using SpotService.Server.MultiNode.Handlers;
using SpotService.Shared;
using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace SpotService.Server.MultiNode.Spots;

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
    public string DisplayName { get; set; } = actorId;

    public int Seen { get; set; }
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}

internal sealed record ScenarioActorCreateReq(string DisplayName);

internal sealed class ScenarioEntrySpot(
    IZLinkEntrySpotContext context,
    EvidenceStore evidence) : IZLinkEntrySpot<ScenarioActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public ValueTask OnCreateActorAsync(
        ScenarioActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!createRequest.IsEmpty) actor.DisplayName = createRequest.Decode<ScenarioActorCreateReq>().DisplayName;

        evidence.Add($"entry-created|rid={evidence.Rid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = actorId;
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
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-admitted|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actorId}");
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));
    }

    public ValueTask OnJoinedActorAsync(ScenarioActor actor, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-joined|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
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

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-created|rid={evidence.Rid}|spot={Context.SpotRid}");
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
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

internal sealed class SpotOnlyUserSpot(
    IZLinkSpotContext context,
    EvidenceStore evidence,
    IZLinkSpotHandleResolver spots) : IZLinkSpot<ScenarioActor>
{
    private int _value;

    public IZLinkSpotContext Context { get; } = context;

    public ValueTask OnInitializeAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-initialize|rid={evidence.Rid}|spot={Context.SpotRid}");
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(
        string actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        _ = request;
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-admitted|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actorId}");
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept());
    }

    public ValueTask OnJoinedActorAsync(ScenarioActor actor, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-joined|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(ScenarioActor actor, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-actor-left|rid={evidence.Rid}|spot={Context.SpotRid}|actor={actor.ActorId}");
        return ValueTask.CompletedTask;
    }

    public async ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(
        ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"spot-created|rid={evidence.Rid}|spot={Context.SpotRid}");
        if (!request.IsEmpty)
        {
            var command = request.Decode<SpotOnlyMeshReq>();
            var target = await spots.ResolveSpotHandleAsync(
                             RoutingId.From(command.TargetSpotRid),
                             cancellationToken)
                         ?? throw new InvalidOperationException(
                             $"Target spot '{command.TargetSpotRid}' has no live location row.");
            var reply = await Context.Outbound
                .RequestToSpot(target, new StateReq("add", 7))
                .Async<StateRes>(cancellationToken);
            Context.Outbound.SendToSpot(target, new StateMsg($"sm-f6-send-{command.Marker}"))
                .TrySubmit();
            evidence.Add(
                $"spot-only-request|rid={evidence.Rid}|source={Context.SpotRid}"
                + $"|target={command.TargetSpotRid}|value={reply.Value}|marker={command.Marker}");
        }

        return ZLinkSpotCreateResponse.Accept();
    }

    public int Add(int delta)
    {
        _value += delta;
        return _value;
    }
}
