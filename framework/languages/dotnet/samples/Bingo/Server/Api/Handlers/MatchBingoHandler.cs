using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Channels;

namespace Bingo.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class MatchBingoHandler(
    IZLinkRouteClient routes,
    ILogger<MatchBingoHandler> logger)
    : IZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes>
{
    public async ValueTask<MatchBingoApiRes> HandleAsync(
        MatchBingoApiReq request,
        IZLinkMessageContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation("api match: request. actor={ActorId}, mode={Mode}",
            request.ActorId, request.Mode);
        var allocated = await routes.RequestToChannel(
                    SampleNames.PlayChannel,
                    new AllocateBingoRoomReq
                    {
                        Mode = request.Mode,
                        ActorId = request.ActorId
                    })
                .Async<AllocateBingoRoomRes>(cancellationToken)
            ;
        logger.LogInformation("api match: allocated. actor={ActorId}, room={RoomId}",
            request.ActorId, allocated.RoomId);

        return new MatchBingoApiRes
        {
            RoomId = allocated.RoomId
        };
    }
}
