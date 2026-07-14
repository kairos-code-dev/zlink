using ObservabilityOps.Server.Play.Domain;
using ObservabilityOps.Server.Play.Support;
using ObservabilityOps.Shared;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Play.Spots;

internal sealed class RoomSpot(IZLinkSpotContext context, EvidenceStore evidence) : IZLinkSpot<PlayerActor>
{
    public IZLinkSpotContext Context { get; } = context;
    public DateTimeOffset? AutoCloseAfter { get; internal set; }

    public ValueTask<ZLinkSpotCreateResponse> OnCreateAsync(ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var create = request.Decode<CreateRoomReq>();
        if (string.Equals(create.Mode, "auto-close", StringComparison.Ordinal))
            AutoCloseAfter = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(7);
        evidence.Add($"room-created|room={Context.SpotRid}|node={Context.NodeRid}");
        return ValueTask.FromResult(ZLinkSpotCreateResponse.Accept());
    }

    public ValueTask<ZLinkSpotActorJoinResult> OnActorJoinAsync(string actorId, ZLinkMessage request,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var joined = new JoinRoomRes(actorId, Context.SpotRid.ToString(), Context.NodeRid.ToString());
        return ValueTask.FromResult(ZLinkSpotActorJoinResult.Accept(joined));
    }

    public ValueTask OnJoinedActorAsync(PlayerActor actor, CancellationToken cancellationToken)
    {
        actor.RoomRid = Context.SpotRid.ToString();
        evidence.Add($"actor-joined|actor={actor.ActorId}|room={actor.RoomRid}|node={Context.NodeRid}");
        return ValueTask.CompletedTask;
    }

    public ValueTask OnLeaveActorAsync(PlayerActor actor, CancellationToken cancellationToken) =>
        ValueTask.CompletedTask;
}
