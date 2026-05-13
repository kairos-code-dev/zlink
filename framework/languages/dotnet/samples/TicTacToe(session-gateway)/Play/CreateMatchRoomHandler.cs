using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
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
        await routes.BindSpotRouteAsync(room.SpotRid, cancellationToken)
            .ConfigureAwait(false);
        return new CreateMatchRoomRes(room.SpotId.Value);
    }
}
