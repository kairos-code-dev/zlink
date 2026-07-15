using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Handlers;
using Systems.Zlink;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Locations;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class MatchBingoHandler(
    IZLinkSpotHandleResolver spots,
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
        var preferredOwner = RoutingId.From(request.ActorNodeRid);
        var playEntrySpot = await spots.ResolveSpotHandleAsync(preferredOwner, cancellationToken)
                            ?? throw new InvalidOperationException(
                                $"Play entry spot '{preferredOwner}' was not found.");
        var allocated = await routes.RequestToSpot(
                    playEntrySpot,
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
