using Bingo.SessionGateway.Shared.Contracts;

namespace Bingo.SessionGateway.Api;

internal sealed class AuthenticatePlayerHandler
    : IZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes>
{
    public ValueTask<AuthenticatePlayerRes> HandleAsync(
        AuthenticatePlayerReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!request.AccessToken.StartsWith("player-", StringComparison.Ordinal))
        {
            return ValueTask.FromResult(new AuthenticatePlayerRes(
                false,
                null,
                null,
                "Access token must be a sample player id."));
        }

        var displayName = request.AccessToken.Replace("player-", "Player ", StringComparison.Ordinal);
        return ValueTask.FromResult(new AuthenticatePlayerRes(
            true,
            request.AccessToken,
            displayName,
            null));
    }
}
