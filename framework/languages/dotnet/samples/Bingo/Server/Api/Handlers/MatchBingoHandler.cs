using Systems.Zlink;
using Systems.Zlink.Codecs.Protobuf;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Bingo.Server.Configuration;
using Bingo.Shared.Contracts;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class MatchBingoHandler(
    IZLinkChannelClient client,
    ILogger<MatchBingoHandler> logger)
    : IZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes>
{
    public async ValueTask<MatchBingoApiRes> HandleAsync(
        MatchBingoApiReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        logger.LogInformation("api match: request. actor={ActorId}, mode={Mode}", request.ActorId, request.Mode);
        var allocated = await client.RequestToChannel(
                SampleNames.PlayChannel,
                new AllocateBingoRoomReq
                {
                    Mode = request.Mode,
                    ActorId = request.ActorId,
                })
            .Async<AllocateBingoRoomRes>(cancellationToken)
            ;
        logger.LogInformation("api match: allocated. actor={ActorId}, room={RoomId}", request.ActorId, allocated.RoomId);

        return new MatchBingoApiRes { RoomId = allocated.RoomId };
    }
}
