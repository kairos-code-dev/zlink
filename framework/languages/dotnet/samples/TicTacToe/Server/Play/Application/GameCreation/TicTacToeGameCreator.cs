using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.Adapters.ZLink.Spots;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Application.GameCreation;

internal sealed class TicTacToeGameCreator(
    IZLinkSpotManager spots,
    SampleSettings settings)
{
    public async ValueTask<CreateGameRes> CreateAsync(
        string gameName,
        CancellationToken cancellationToken)
    {
        var created = await spots.CreateAsync<TicTacToeGame>(cancellationToken);
        if (created.State != ZLinkSpotCreateState.Created)
        {
            throw new InvalidOperationException("TicTacToe game spot creation was rejected.");
        }

        return new CreateGameRes(
            created.SpotRid.ToHex(),
            settings.PlayEndpoint,
            gameName);
    }
}
