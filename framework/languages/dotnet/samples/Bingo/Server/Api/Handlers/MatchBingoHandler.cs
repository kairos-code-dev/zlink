using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace Bingo.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class MatchBingoHandler(
    IZLinkRouteClient routes,
    ILogger<MatchBingoHandler> logger)
    : IZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes>
{
    public async ValueTask<MatchBingoApiRes> HandleAsync(
        MatchBingoApiReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation("api match: request. actor={ActorId}, mode={Mode}, actorNode={ActorNodeRid}",
            request.ActorId, request.Mode, request.ActorNodeRid);
        var allocated = await routes.Request(
                    SampleNames.PlayChannel,
                    RoutingId.From(request.ActorNodeRid),
                    new AllocateBingoRoomReq
                    {
                        Mode = request.Mode,
                        ActorId = request.ActorId,
                        PreferredOwnerNodeRid = request.ActorNodeRid
                    })
                .Async<AllocateBingoRoomRes>(cancellationToken)
            ;
        logger.LogInformation("api match: allocated. actor={ActorId}, room={RoomId}, owner={OwnerNodeRid}",
            request.ActorId, allocated.RoomId, allocated.RoomOwnerNodeRid);

        return new MatchBingoApiRes
        {
            RoomId = allocated.RoomId,
            RoomOwnerNodeRid = allocated.RoomOwnerNodeRid
        };
    }
}