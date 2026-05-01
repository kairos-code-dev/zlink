using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink.Framework.Handlers;
using Zlink.Framework.Spots;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class CreateMatchRoomHandler(
    IZLinkSpotManager spots,
    RegistryPlayRoutePublisher routes)
{
    [ZLinkRequest]
    public async ValueTask<CreateMatchRoomRes> CreateMatchRoom(
        CreateMatchRoomReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var room = await spots.CreateAsync(SampleNames.GameSpotType, cancellationToken)
            .ConfigureAwait(false);
        var matchId = room.SpotRid.ToHex();
        await routes.BindMatchAsync(matchId, cancellationToken)
            .ConfigureAwait(false);
        return new CreateMatchRoomRes(matchId);
    }
}
