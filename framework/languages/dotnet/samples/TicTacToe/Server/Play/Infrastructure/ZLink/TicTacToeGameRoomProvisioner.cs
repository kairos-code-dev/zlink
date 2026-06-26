using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Application.GameCreation;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.EntrySpot;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.TicTacToeGameSpot;
using Systems.Zlink;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Infrastructure.ZLink;

internal sealed class TicTacToeGameRoomProvisioner(
    IZLinkSpotManager spots,
    IRoomRouteStore routes,
    SampleSettings settings) : ITicTacToeGameRoomProvisioner
{
    public async ValueTask ProvisionAsync(
        string roomId,
        CancellationToken cancellationToken)
    {
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
    }
}
