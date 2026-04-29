namespace TicTacToe.Server.Api.Handlers;

sealed class AuthenticatePlayerHandler(ILogger<AuthenticatePlayerHandler> logger)
{
    [ZLinkRequest]
    public AuthenticatePlayerRes AuthenticateReq(
        AuthenticatePlayerReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        _ = cancellationToken;

        var playerId = request.AccessToken.Trim();
        if (string.IsNullOrWhiteSpace(playerId))
        {
            throw new InvalidOperationException("Authentication token is empty.");
        }

        logger.LogInformation(
            "play -> api: authenticate accepted. player={PlayerId}",
            playerId);
        return new AuthenticatePlayerRes(playerId);
    }
}
