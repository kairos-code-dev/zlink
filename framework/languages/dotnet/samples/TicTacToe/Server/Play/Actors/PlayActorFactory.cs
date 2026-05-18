namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayActorFactory(ILogger<PlayActor> logger) : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        IZLinkActorContext context,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new PlayActor(actorId, context, logger));
    }
}
