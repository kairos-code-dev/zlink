using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Adapters.ZLink.Spots;
using TicTacToe.Shared.Contracts;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Application.GameCreation;

internal sealed class TicTacToeGameCreator(
    IZLinkSpotManager spots,
    IRoomRouteStore routes,
    SampleSettings settings)
{
    public async ValueTask<CreateGameRes> CreateAsync(
        string gameName,
        CancellationToken cancellationToken)
    {
        var roomId = $"room-{Guid.NewGuid():N}";
        await spots.GetOrCreateAsync<TicTacToeGame>(
            RoutingId.From(roomId),
            cancellationToken);

        await routes.SaveAsync(
            roomId,
            new RoomRoute(
                SampleNodes.PlaySpot,
                settings.PlaySpotNodeRid,
                roomId,
                nameof(ZLinkSpotKind.User)),
            cancellationToken);

        return new CreateGameRes(
            roomId,
            settings.PlayEndpoint,
            settings.PlayEndpoints,
            settings.PlayNodes,
            gameName,
            SampleDefaults.RequiredLevel);
    }
}
