using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.Infrastructure.ZLink.Actors;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Spots.Handlers;

internal sealed class PlayActorLeaveGameHandler(ILogger<PlayActorLeaveGameHandler> logger)
    : IZLinkSpotActorSendHandler<TicTacToeGame, PlayActor, LeaveGameReq>
{
    public async ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorSendContext context,
        LeaveGameReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        var roomId = actor.RequireJoinedRoom();
        if (!string.Equals(roomId, message.RoomId, StringComparison.Ordinal))
        {
            throw new InvalidOperationException($"Actor is joined to '{roomId}', not '{message.RoomId}'.");
        }

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
