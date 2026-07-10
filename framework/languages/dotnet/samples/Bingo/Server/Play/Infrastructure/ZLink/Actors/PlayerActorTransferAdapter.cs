using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Actors;

internal sealed class PlayerActorTransferAdapter : IZLinkActorTransferAdapter<PlayerActor>
{
    public ValueTask<ZLinkMessage> TransferOutAsync(
        PlayerActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkMessage.From(new PlayerActorTransferState(
            actor.DisplayName,
            actor.RoomId,
            actor.DestroyAfterEntrySpotJoin,
            actor.Disconnected)));
    }

    public ValueTask<PlayerActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var transferred = state.Decode<PlayerActorTransferState>();
        var actor = new PlayerActor(actorId, context);
        actor.SetDisplayName(transferred.DisplayName);
        if (!string.IsNullOrEmpty(transferred.RoomId)) actor.JoinRoom(transferred.RoomId);
        if (transferred.DestroyAfterEntrySpotJoin) actor.MarkForDestroyAfterRoomLeave();
        if (transferred.Disconnected) actor.MarkDisconnected();
        return ValueTask.FromResult(actor);
    }

    private sealed record PlayerActorTransferState(
        string DisplayName,
        string RoomId,
        bool DestroyAfterEntrySpotJoin,
        bool Disconnected);
}
