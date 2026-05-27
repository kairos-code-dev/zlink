using Zlink.Framework.Contracts.Actors;

namespace Bingo.Server.Play.Actors;

internal sealed class PlayerActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public string DisplayName { get; private set; } = actorId;

    public string RoomId { get; private set; } = string.Empty;

    public IZLinkActorContext Context { get; } = context;

    public void SetDisplayName(string displayName)
    {
        DisplayName = displayName;
    }

    public void JoinRoom(string roomId)
    {
        RoomId = roomId;
    }

}
