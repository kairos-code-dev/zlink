using Zlink.Framework.Contracts.Handlers;
using Bingo.Server.Play.Application.RoomAllocation;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Adapters.ZLink.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class AllocateBingoRoomHandler(
    BingoRoomAllocator allocator,
    ILogger<AllocateBingoRoomHandler> logger)
    : IZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes>
{
    public async ValueTask<AllocateBingoRoomRes> HandleAsync(
        AllocateBingoRoomReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        logger.LogInformation(
            "play allocate: request. actor={ActorId}, mode={Mode}",
            request.ActorId,
            request.Mode);
        var roomId = await allocator.AllocateAsync(request.Mode, request.ActorId, cancellationToken);
        logger.LogInformation(
            "play allocate: allocated. actor={ActorId}, room={RoomId}",
            request.ActorId,
            roomId);

        return new AllocateBingoRoomRes { RoomId = roomId };
    }
}
