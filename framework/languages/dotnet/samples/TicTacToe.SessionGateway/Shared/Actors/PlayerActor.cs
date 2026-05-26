using Zlink.Framework.Contracts.Actors;

namespace TicTacToe.SessionGateway.Shared.Actors;

public sealed class PlayerActor(
    string actorId,
    IZLinkActorContext context) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;
}
