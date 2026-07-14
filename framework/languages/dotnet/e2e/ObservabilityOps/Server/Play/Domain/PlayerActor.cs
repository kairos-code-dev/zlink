using Zlink.Framework.Contracts.Actors;

namespace ObservabilityOps.Server.Play.Domain;

internal sealed class PlayerActor(string actorId, IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;
    public IZLinkActorContext Context { get; } = context;
    public string RoomRid { get; set; } = string.Empty;
}

internal sealed record PlayerTransferState(string RoomRid);
