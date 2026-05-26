using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Actors;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.EntrySpot.Handlers;

internal sealed class BingoEntrySpotActorJoinedHandler(
    ILogger<BingoEntrySpotActorJoinedHandler> logger)
    : IZLinkSpotPostActorJoinedHandler<BingoEntrySpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}, kind={Kind}",
            actor.ActorId,
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
