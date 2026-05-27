using Zlink.Framework.Contracts.Actors;

namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayActor(
    string actorId,
    IZLinkActorContext context)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; } = context;

    public string GameId { get; private set; } = string.Empty;

    public void JoinGame(string gameId)
    {
        if (string.IsNullOrWhiteSpace(gameId))
        {
            throw new ArgumentException("Game id must not be empty.", nameof(gameId));
        }

        GameId = gameId;
    }

    public string RequireJoinedGame()
    {
        if (!Context.IsJoined || string.IsNullOrEmpty(GameId))
        {
            throw new InvalidOperationException("Actor has not joined a game.");
        }

        return GameId;
    }

}
