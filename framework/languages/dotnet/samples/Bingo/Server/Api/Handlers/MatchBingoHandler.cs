using Bingo.Shared.Configuration;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class MatchBingoHandler(IZLinkClient client)
    : IZLinkRequestHandler<MatchBingoApiReq, MatchBingoApiRes>
{
    public async ValueTask<MatchBingoApiRes> HandleAsync(
        MatchBingoApiReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var allocated = await client.Request(
                SampleNames.PlayChannel,
                new AllocateBingoRoomReq(request.Mode))
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<AllocateBingoRoomRes>(cancellationToken)
            ;

        return new MatchBingoApiRes(allocated.RoomId);
    }
}
