using TicTacToe.Server.Play.Infrastructure.ZLink.Actors;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Spots.TicTacToeGameSpot.Handlers;

[ZLinkSpotActorSendHandler(nameof(LeaveGameReq))]
internal sealed class PlayActorLeaveGameHandler(ILogger<PlayActorLeaveGameHandler> logger)
    : IZLinkSpotActorSendHandler<TicTacToeGame, PlayActor, LeaveGameReq>
{
    public async ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        IZLinkMessageContext context,
        LeaveGameReq message,
        CancellationToken cancellationToken)
    {
        var roomId = actor.RequireJoinedRoom();
        if (!string.Equals(roomId, message.RoomId, StringComparison.Ordinal))
            throw new InvalidOperationException($"Actor is joined to '{roomId}', not '{message.RoomId}'.");

        logger.LogInformation(
            "actor: LeaveGameReq received. actor={ActorId}, roomId={RoomId}",
            actor.ActorId,
            message.RoomId);

        await spot.LeaveGameAsync(actor, message.RoomId, cancellationToken);

        logger.LogInformation(
            "actor: LeaveGameReq completed. actor={ActorId}, roomId={RoomId}",
            actor.ActorId,
            message.RoomId);
    }
}
