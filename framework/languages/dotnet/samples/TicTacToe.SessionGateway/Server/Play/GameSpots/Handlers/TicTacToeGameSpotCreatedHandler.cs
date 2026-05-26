using Microsoft.Extensions.Logging;
using Systems.Zlink;
using TicTacToe.SessionGateway.Server.Play.GameSpots;

namespace TicTacToe.SessionGateway.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameSpotCreatedHandler(
    ILogger<TicTacToeGameSpotCreatedHandler> logger)
{
    public ValueTask HandleAsync(
        TicTacToeGameSpot spot,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: created. matchId={MatchId}, createParts={CreatePartCount}",
            spot.Context.SpotRid.ToHex(),
            createParts.Count);
        return ValueTask.CompletedTask;
    }
}
