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

        logger.LogInformation(
            "play -> api: authenticate accepted. player={ActorId}",
            actorId);
        return ValueTask.FromResult(new AuthenticatePlayerRes(actorId));
    }
}
