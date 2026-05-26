using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
internal sealed class CreateMatchRoomHandler(IZLinkSpotManager spots)
{
    [ZLinkRequest]
    public async ValueTask<CreateMatchRoomRes> CreateMatchRoom(
        CreateMatchRoomReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        cancellationToken.ThrowIfCancellationRequested();
        var room = await spots.CreateAsync<TicTacToeGameSpot>(cancellationToken)
            ;
        return new CreateMatchRoomRes(room.SpotRid.ToHex());
    }
}
