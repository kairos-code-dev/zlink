using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionGateway.Api;

[ZLinkHandlerGroup("api")]
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
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<CreateMatchRoomRes>(cancellationToken)
            .ConfigureAwait(false);

        return new CreateMatchRes(room.MatchId, ownerActorId);
    }
}
