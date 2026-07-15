using ObservabilityOps.Server.Play.Domain;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace ObservabilityOps.Server.Play.Infrastructure;

internal sealed class PlayerActor(
    Player player,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId => Player.PlayerId;

    public IZLinkActorContext Context { get; } = context;

    public Player Player { get; } = player;
}

internal sealed record PlayerTransferState(string RoomRid);

internal sealed class PlayerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(string actorId, IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new PlayerActor(new Player(actorId), context));
    }
}

internal sealed class PlayerActorTransferAdapter : IZLinkActorTransferAdapter<PlayerActor>
{
    public ValueTask<ZLinkMessage> TransferOutAsync(PlayerActor actor, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkMessage.From(new PlayerTransferState(actor.Player.RoomRid)));
    }

    public ValueTask<PlayerActor> TransferInAsync(string actorId, IZLinkActorContext context,
        ZLinkMessage state, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var transferred = state.Decode<PlayerTransferState>();
        var player = new Player(actorId);
        if (!string.IsNullOrWhiteSpace(transferred.RoomRid)) player.JoinRoom(transferred.RoomRid);
        return ValueTask.FromResult(new PlayerActor(player, context));
    }
}
