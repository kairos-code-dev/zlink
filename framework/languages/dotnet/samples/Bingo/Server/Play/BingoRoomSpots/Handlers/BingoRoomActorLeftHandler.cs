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

internal sealed class BingoRoomActorLeftHandler(
    ILogger<BingoRoomActorLeftHandler> logger)
    : IZLinkSpotActorLeftHandler<BingoRoomSpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "bingo room: actor left. room={RoomId}, actor={ActorId}, nextSpot={CurrentSpotRid}, epoch={CommitEpoch}",
            spot.Context.SpotRid.ToHex(),
            actor.ActorId,
            info.CurrentSpotRid?.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
