using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Server.Play.GameSpots;

namespace TicTacToe.SessionGateway.Server.Play.GameSpots.Handlers;

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
