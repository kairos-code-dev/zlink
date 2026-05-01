using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using Zlink.Framework.Channels;
using Zlink.Framework.Handlers;

namespace TicTacToe.SessionActorDispatch.Api;

internal sealed class CreateMatchHandler(IZLinkClient client)
{
    [ZLinkRequest]
    public async ValueTask<CreateMatchRes> CreateMatch(
        CreateMatchReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        var ownerActorId = request.OwnerActorId?.Trim();
        if (string.IsNullOrWhiteSpace(ownerActorId))
        {
            throw new InvalidOperationException("Match owner actor id must not be empty.");
        }

        var matchName = string.IsNullOrWhiteSpace(request.MatchName)
            ? SampleNames.MatchId
            : request.MatchName;
        var room = await client.Request(
                SampleNames.PlayChannel,
                new CreateMatchRoomReq(matchName))
            .WithTimeout(SampleTimings.RequestTimeout)
            .Async<CreateMatchRoomRes>(cancellationToken)
            .ConfigureAwait(false);

        return new CreateMatchRes(room.MatchId, ownerActorId);
    }
}
