using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.EntrySpot;

namespace Bingo.Server.Play.Infrastructure.ZLink.Handlers;

internal sealed class AllocateBingoRoomHandler(
    BingoRoomAllocator allocator,
    IZLinkSpotManager spots,
    ILogger<AllocateBingoRoomHandler> logger)
    : IZLinkSpotRequestHandler<BingoEntrySpot, AllocateBingoRoomReq, AllocateBingoRoomRes>
{
    public async ValueTask<AllocateBingoRoomRes> HandleAsync(
        BingoEntrySpot entrySpot,
        AllocateBingoRoomReq request,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "play allocate: request. actor={ActorId}, mode={Mode}, preferredOwner={PreferredOwnerNodeRid}",
            request.ActorId,
            request.Mode,
            request.PreferredOwnerNodeRid);
        var reservation = await allocator.AllocateAsync(
            request.Mode,
            request.ActorId,
            request.PreferredOwnerNodeRid,
            cancellationToken);
        logger.LogInformation(
            "play allocate: allocated. actor={ActorId}, room={RoomId}, owner={OwnerNodeRid}",
            request.ActorId,
            reservation.RoomId,
            reservation.OwnerPlayNodeRid);
        if (reservation.NewRoomSettings is not null)
            await spots.GetOrCreateAsync<BingoRoom, BingoRoomSettingsPayload>(
                RoutingId.From(reservation.RoomId),
                BingoRoomSettingsPayloadMapper.ToPayload(reservation.NewRoomSettings),
                cancellationToken);

        return new AllocateBingoRoomRes
        {
            RoomId = reservation.RoomId,
            RoomOwnerNodeRid = reservation.OwnerPlayNodeRid
        };
    }
}
