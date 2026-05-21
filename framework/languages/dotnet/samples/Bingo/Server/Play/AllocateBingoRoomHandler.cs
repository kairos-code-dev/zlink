using Bingo.Shared.Contracts;

namespace Bingo.Server.Play;

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
            .ConfigureAwait(false);
        return new AllocateBingoRoomRes(roomId);
    }
}
