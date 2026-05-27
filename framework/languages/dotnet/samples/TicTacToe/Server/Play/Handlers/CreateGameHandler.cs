using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play.GameSpots;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Handlers;

[ZLinkHandlerGroup("play")]
sealed class CreateGameHandler(
    IZLinkSpotManager spots,
    SampleSettings settings,
    ILogger<CreateGameHandler> logger)
{
    [ZLinkRequest]
    public async ValueTask<CreateGameRes> CreateAsync(
        CreateGameReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        logger.LogInformation(
            "api -> play: CreateGameReq received. game={GameName}",
            request.GameName);

        var created = await spots.CreateAsync<TicTacToeGame>(cancellationToken);
        logger.LogInformation(
            "play: TicTacToeGame spot created. gameId={GameId}, endpoint={Endpoint}",
            created.SpotRid.ToHex(),
            settings.PlayEndpoint);

        return new CreateGameRes(
            created.SpotRid.ToHex(),
            settings.PlayEndpoint,
            request.GameName);
    }
}
