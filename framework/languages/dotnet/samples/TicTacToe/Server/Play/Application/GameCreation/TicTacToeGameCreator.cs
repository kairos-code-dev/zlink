using TicTacToe.Server.Configuration;
using TicTacToe.Shared.Contracts;

namespace TicTacToe.Server.Play.Application.GameCreation;

internal sealed class TicTacToeGameCreator(
    ITicTacToeGameRoomProvisioner rooms,
    SampleSettings settings)
{
    public async ValueTask<CreateGameRes> CreateAsync(
        string gameName,
        CancellationToken cancellationToken)
    {
        var roomId = $"room-{Guid.NewGuid():N}";
        await rooms.ProvisionAsync(roomId, cancellationToken);

        return new CreateGameRes(
            roomId,
            settings.PlayEndpoint,
            settings.PlayEndpoints,
            settings.PlayNodes,
            gameName,
            SampleDefaults.RequiredLevel);
    }
}

internal interface ITicTacToeGameRoomProvisioner
{
    ValueTask ProvisionAsync(
        string roomId,
        CancellationToken cancellationToken);
}