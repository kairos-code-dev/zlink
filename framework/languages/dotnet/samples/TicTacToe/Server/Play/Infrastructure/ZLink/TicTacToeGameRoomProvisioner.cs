using Systems.Zlink;
using TicTacToe.Server.Play.Application.GameCreation;
using TicTacToe.Server.Play.Infrastructure.ZLink.Spots.TicTacToeGameSpot;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Infrastructure.ZLink;

internal sealed class TicTacToeGameRoomProvisioner(
    IZLinkSpotManager spots) : ITicTacToeGameRoomProvisioner
{
    public async ValueTask ProvisionAsync(
        string roomId,
        CancellationToken cancellationToken)
    {
        await spots.GetOrCreateAsync<TicTacToeGame>(
            RoutingId.From(roomId),
            cancellationToken);
    }
}
