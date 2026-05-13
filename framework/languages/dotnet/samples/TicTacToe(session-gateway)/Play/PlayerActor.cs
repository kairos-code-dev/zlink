using Zlink.Framework.Actors;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class PlayerActor(string actorId) : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; set; } = default!;

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        return ValueTask.CompletedTask;
    }
}
