using Zlink.Framework.Contracts.Actors;

namespace Bingo.Server.Play.Infrastructure.ZLink.Actors;

internal sealed class PlayerActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public string DisplayName { get; private set; } = actorId;

    public string RoomId { get; private set; } = string.Empty;

    public bool DestroyAfterEntrySpotJoin { get; private set; }

    public bool Disconnected { get; private set; }

    public IZLinkActorContext Context { get; } = context;

    public void SetDisplayName(string displayName)
    {
        DisplayName = displayName;
    }

    public void JoinRoom(string roomId)
    {
        RoomId = roomId;
    }

    public void MarkForDestroyAfterRoomLeave()
    {
        DestroyAfterEntrySpotJoin = true;
    }

    public void MarkDisconnected()
    {
        Disconnected = true;
    }
}
