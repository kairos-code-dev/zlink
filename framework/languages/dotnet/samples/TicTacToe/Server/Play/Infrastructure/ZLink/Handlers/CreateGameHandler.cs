using TicTacToe.Server.Play.Application.GameCreation;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Handlers;

internal sealed class CreateGameHandler(
    TicTacToeGameCreator games,
    ILogger<CreateGameHandler> logger)
    : IZLinkRequestHandler<CreateGameReq, CreateGameRes>
{
    public async ValueTask<CreateGameRes> HandleAsync(
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
            "play: TicTacToeGame spot created. roomId={RoomId}, endpoint={Endpoint}",
            created.RoomId,
            created.OwnerPlayEndpoint);
        return created;
    }
}