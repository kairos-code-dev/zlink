using PlayHouseRoomEcho.Server.Actors;

namespace PlayHouseRoomEcho.Server.Play.Handlers;

sealed class GameRoomJoinHandler(ILogger<GameRoomJoinHandler> logger)
    : IZLinkSpotActorJoinHandler<GameRoom, PlayerActor, JoinRoomRequest, JoinRoomReply>
{
    public async ValueTask<JoinRoomReply> HandleAsync(
        GameRoom spot,
        PlayerActor player,
        JoinRoomRequest request,
        CancellationToken cancellationToken)
    {
        player.RoomId = request.RoomId;
        player.PlayerId = request.PlayerId;
        logger.LogInformation(
            "GameRoom: actor join accepted. actor={ActorKey}, roomId={RoomId}, player={PlayerId}",
            player.ActorKey,
            request.RoomId,
            request.PlayerId);

        await player.OnAttachedAsync(spot, cancellationToken);
        return new JoinRoomReply(request.RoomId, request.PlayerId);
    }
}
