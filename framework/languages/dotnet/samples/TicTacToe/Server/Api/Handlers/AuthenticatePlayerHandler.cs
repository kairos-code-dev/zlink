using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.Server.Api.Handlers;

sealed class AuthenticatePlayerHandler(ILogger<AuthenticatePlayerHandler> logger)
    : IZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes>
{
    public ValueTask<AuthenticatePlayerRes> HandleAsync(
        AuthenticatePlayerReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;

        var actorId = request.AccessToken.Trim();
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Authentication token is empty.");
        }

        var player = CreatePlayer(actorId);

        logger.LogInformation(
            "play -> api: authenticate accepted. player={ActorId}, level={Level}, wins={Wins}",
            player.ActorId,
            player.Level,
            player.Wins);
        return ValueTask.FromResult(new AuthenticatePlayerRes(player));
    }

    private static PlayerInfo CreatePlayer(string actorId)
    {
        return actorId switch
        {
            "player-x" => new PlayerInfo(actorId, "Player X", Level: 5, Wins: 99),
            "player-o" => new PlayerInfo(actorId, "Player O", Level: 4, Wins: 12),
            "observer" => new PlayerInfo(actorId, "Observer", Level: 1, Wins: 0),
            _ => new PlayerInfo(actorId, actorId, Level: 3, Wins: 0),
        };
    }
}
