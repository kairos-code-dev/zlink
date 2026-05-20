using Bingo.SessionGateway.Shared.Configuration;
using Bingo.SessionGateway.Shared.Contracts;

namespace Bingo.SessionGateway.Api;

internal sealed class MatchBingoHandler(IZLinkClientServerClient client)
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
            .ConfigureAwait(false);
        return new MatchBingoApiRes(allocated.RoomId);
    }
}
