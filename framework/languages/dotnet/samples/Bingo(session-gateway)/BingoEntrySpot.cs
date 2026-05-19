namespace Bingo.SessionGateway;

internal sealed class BingoEntrySpot
{
    public BingoRoomJoinRes JoinRoom(
        PlayerActor actor,
        BingoRoomSpot room)
    {
        if (actor.JoinedRoomId is not null && actor.JoinedRoomId != room.RoomId)
        {
            throw new InvalidOperationException(
                $"Actor {actor.ActorId} is already joined to room {actor.JoinedRoomId}.");
        }

        return room.Join(actor);
    }
}
