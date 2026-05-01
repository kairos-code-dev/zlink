namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayActorFactory(ILogger<PlayActor> logger) : IZLinkActorFactory
{
    public ValueTask<IZLinkActor> CreateAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return ValueTask.FromResult<IZLinkActor>(new PlayActor(actorId, logger));
    }
}
