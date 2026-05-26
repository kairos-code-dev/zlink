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
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomActorJoinedHandler(
    ILogger<BingoRoomActorJoinedHandler> logger)
    : IZLinkSpotPostActorJoinedHandler<BingoRoomSpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "bingo room: actor joined. room={RoomId}, actor={ActorId}, kind={Kind}",
            spot.Context.SpotRid.ToHex(),
            actor.ActorId,
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
