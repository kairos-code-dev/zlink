using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class AllocateBingoRoomHandler(BingoRoomDirectory rooms)
    : IZLinkRequestHandler<AllocateBingoRoomReq, AllocateBingoRoomRes>
{
    public async ValueTask<AllocateBingoRoomRes> HandleAsync(
        AllocateBingoRoomReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var roomId = await rooms.AllocateAsync(request.Mode, cancellationToken)
            ;
        return new AllocateBingoRoomRes(roomId);
    }
}
