using Zlink.Framework.Contracts.Actors;

namespace TicTacToe.Server.Play.Adapters.ZLink.Actors;

internal sealed class PlayActor(
    string actorId,
    IZLinkActorContext context)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public string RoomId { get; private set; } = string.Empty;

    public void JoinRoom(string roomId)
    {
        if (string.IsNullOrWhiteSpace(roomId))
        {
            throw new ArgumentException("Room id must not be empty.", nameof(roomId));
        }

        RoomId = roomId;
    }

    public string RequireJoinedRoom()
    {
        if (!Context.IsJoined || string.IsNullOrEmpty(RoomId))
        {
            throw new InvalidOperationException("Actor has not joined a room.");
        }

        return RoomId;
    }

}
