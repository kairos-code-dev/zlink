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

        var actorId = request.AccessToken.Trim();
        if (string.IsNullOrWhiteSpace(actorId))
        {
            throw new InvalidOperationException("Authentication token is empty.");
        }

        logger.LogInformation(
            "play -> api: authenticate accepted. player={ActorId}",
            actorId);
        return new AuthenticatePlayerRes(actorId);
    }
}
