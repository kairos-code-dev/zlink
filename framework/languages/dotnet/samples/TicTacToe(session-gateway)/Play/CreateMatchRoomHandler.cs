using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink.Framework.Handlers;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class CreateMatchRoomHandler(
    TicTacToeGameService games,
    RegistryPlayRouteStore routes,
    SampleTopology topology)
{
    [ZLinkRequest]
    public async ValueTask<CreateMatchRoomRes> CreateMatchRoom(
        CreateMatchRoomReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var room = games.Create(request.MatchName);
        await routes.BindMatchAsync(
                room.MatchId,
                new ZLinkPlayRoute(SampleNames.RouterChannel, topology.PlayRid),
                cancellationToken)
            .ConfigureAwait(false);
        return new CreateMatchRoomRes(room.MatchId);
    }
}
