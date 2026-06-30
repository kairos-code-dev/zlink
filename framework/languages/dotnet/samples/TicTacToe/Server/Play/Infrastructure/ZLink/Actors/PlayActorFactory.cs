using Zlink.Framework.Contracts.Actors;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Actors;

internal sealed class PlayActorFactory : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new PlayActor(actorId, context));
    }
}