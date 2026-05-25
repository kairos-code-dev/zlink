using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Play.Actors;
using Bingo.Server.Play.BingoRoomSpots;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.EntrySpot.Handlers;

internal sealed class BingoEntrySpotActorJoinedHandler(
    ILogger<BingoEntrySpotActorJoinedHandler> logger)
    : IZLinkEntrySpotActorJoinedHandler<BingoEntrySpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}, currentSpot={CurrentSpotRid}, epoch={CommitEpoch}",
            actor.ActorId,
            info.CurrentSpotRid?.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
