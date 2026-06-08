using TicTacToe.Server.Play.Application.GameCreation;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.Server.Play.Adapters.ZLink.Handlers;

[ZLinkHandlerGroup("play")]
sealed class CreateGameHandler(
    TicTacToeGameCreator games,
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

        var created = await games.CreateAsync(request.GameName, cancellationToken);
        logger.LogInformation(
            "play: TicTacToeGame spot created. gameId={GameId}, endpoint={Endpoint}",
            created.GameId,
            created.PlayEndpoint);
        return created;
    }
}
