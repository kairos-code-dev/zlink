using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using Zlink.Framework.Channels;
using Zlink.Framework.Handlers;

namespace TicTacToe.SessionGateway.Api;

internal sealed class CreateMatchHandler(IZLinkClientServerClient client)
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

        var room = await client.Request(
                SampleNames.PlayChannel,
                new CreateMatchRoomReq())
            .WithTimeout(SampleTimings.RequestTimeout)
            .Submit<CreateMatchRoomRes>(cancellationToken)
            .ConfigureAwait(false);

        return new CreateMatchRes(room.MatchId, ownerActorId);
    }
}
