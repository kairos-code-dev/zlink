using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Channels;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class AllocateBingoRoomHandler(
    BingoRoomAllocator allocator,
    ILogger<AllocateBingoRoomHandler> logger)
    : IZLinkRouteRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes>
{
    public async ValueTask<AllocateBingoRoomRes> HandleAsync(
        AllocateBingoRoomReq request,
        ZLinkRouteRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
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

        return new AllocateBingoRoomRes
        {
            RoomId = reservation.RoomId,
            RoomOwnerNodeRid = reservation.OwnerPlayNodeRid,
        };
    }
}
