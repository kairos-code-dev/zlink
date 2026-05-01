using Zlink.Framework.Actors;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class PlayerActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new PlayerActor(actorId));
    }
}
