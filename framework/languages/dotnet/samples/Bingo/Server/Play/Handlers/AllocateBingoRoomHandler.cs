using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class AllocateBingoRoomHandler(
    BingoRoomDirectory rooms,
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
        var roomId = await rooms.AllocateAsync(request.Mode, request.ActorId, cancellationToken)
            ;
        logger.LogInformation(
            "play allocate: allocated. actor={ActorId}, room={RoomId}",
            request.ActorId,
            roomId);

        return new AllocateBingoRoomRes(roomId);
    }
}
