namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayActorFactory(ILogger<PlayActor> logger)
{
    public PlayActor Create(string actorId)
        => new(actorId, logger);
}
