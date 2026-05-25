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
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomJoinHandler
    : IZLinkSpotActorJoinHandler<BingoRoomSpot, PlayerActor, BingoRoomJoinReq, BingoRoomJoinRes>
{
    public async ValueTask<BingoRoomJoinRes> HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        BingoRoomJoinReq request,
        CancellationToken cancellationToken)
    {
        return await spot.JoinAsync(actor, request, cancellationToken);
    }
}
