using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Messaging;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Actors;

internal sealed class PlayActorTransferAdapter : IZLinkActorTransferAdapter<PlayActor>
{
    public ValueTask<ZLinkMessage> TransferOutAsync(
        PlayActor actor,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult(ZLinkMessage.From(new PlayActorTransferState(
            actor.RoomId,
            actor.Player,
            actor.DestroyAfterEntrySpotJoin,
            actor.Disconnected)));
    }

    public ValueTask<PlayActor> TransferInAsync(
        string actorId,
        IZLinkActorContext context,
        ZLinkMessage state,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var transferred = state.Decode<PlayActorTransferState>();
        var actor = new PlayActor(actorId, context);
        if (transferred.Player is not null) actor.ApplyPlayer(transferred.Player);
        if (!string.IsNullOrEmpty(transferred.RoomId)) actor.JoinRoom(transferred.RoomId);
        if (transferred.DestroyAfterEntrySpotJoin) actor.MarkForDestroyAfterRoomLeave();
        if (transferred.Disconnected) actor.MarkDisconnected();
        return ValueTask.FromResult(actor);
    }

    private sealed record PlayActorTransferState(
        string RoomId,
        PlayerInfo? Player,
        bool DestroyAfterEntrySpotJoin,
        bool Disconnected);
}
