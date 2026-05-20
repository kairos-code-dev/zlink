using Zlink.Framework.Contracts.Actors;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class PlayerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new PlayerActor(actorId, context));
    }
}
