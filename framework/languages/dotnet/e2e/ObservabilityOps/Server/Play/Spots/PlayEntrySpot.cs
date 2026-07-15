using ObservabilityOps.Server.Play.Infrastructure;
using ObservabilityOps.Server.Play.Support;
using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Play.Spots;

internal sealed class PlayEntrySpot(IZLinkEntrySpotContext context, EvidenceStore evidence)
    : IZLinkEntrySpot<PlayerActor>
{
    public IZLinkEntrySpotContext Context { get; } = context;

    public ValueTask OnCreateActorAsync(PlayerActor actor, ZLinkMessage createRequest,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        evidence.Add($"actor-created|actor={actor.ActorId}|node={Context.NodeRid}");
        return ValueTask.CompletedTask;
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(string actorId, ZLinkMessage request,
        CancellationToken cancellationToken) =>
        ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(request));

    public ValueTask OnJoinedActorAsync(PlayerActor actor, CancellationToken cancellationToken)
    {
        evidence.Add($"actor-entry-joined|actor={actor.ActorId}|node={Context.NodeRid}|previous-room={actor.Player.RoomRid}");
        actor.Context.BoundSession.Send(new PlayerMovedNotify(actor.ActorId, Context.NodeRid.ToString()))
            .Submit(cancellationToken);
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(PlayerActor actor, CancellationToken cancellationToken) =>
        ValueTask.CompletedTask;
}
