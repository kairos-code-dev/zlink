using Systems.Zlink;

namespace TicTacToe.Server.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameCreatedHandler(ILogger<TicTacToeGameCreatedHandler> logger)
{
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: created. gameId={GameId}, createParts={CreatePartCount}",
            spot.Context.SpotRid.ToHex(),
            createParts.Count);
        return ValueTask.CompletedTask;
    }
}
