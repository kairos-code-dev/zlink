using TicTacToe.Server.Play.Actors.Handlers;

namespace TicTacToe.Server.Play.Actors;

internal sealed class PlayActor(
    string actorId,
    ILogger<PlayActor> logger)
    : IZLinkActor
{
    public string ActorId { get; } = actorId;

    public IZLinkActorContext Context { get; set; } = default!;

    public string GameId { get; private set; } = string.Empty;

    public void Configure()
    {
        Context.AddPacket<PlayActorJoinGameHandler>();
        Context.AddPacket<PlayActorPlaceMarkHandler>();
    }

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

    public ValueTask OnDisconnectedAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "play actor: stream disconnected. actor={ActorId}, gameId={GameId}",
            ActorId,
            GameId);
        return ValueTask.CompletedTask;
    }
}
