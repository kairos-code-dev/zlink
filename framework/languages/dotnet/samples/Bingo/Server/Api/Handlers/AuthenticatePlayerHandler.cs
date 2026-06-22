using Zlink.Framework.Contracts.Handlers;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class AuthenticatePlayerHandler
    : IZLinkRequestHandler<AuthenticatePlayerReq, AuthenticatePlayerRes>
{
    public ValueTask<AuthenticatePlayerRes> HandleAsync(
        AuthenticatePlayerReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (!request.AccessToken.StartsWith("player-", StringComparison.Ordinal)
            && !string.Equals(request.AccessToken, BingoSamplePlayers.Observer, StringComparison.Ordinal))
        {
            return ValueTask.FromResult(new AuthenticatePlayerRes
            {
                Accepted = false,
                Reason = "Access token must be a sample player id.",
            });
        }

        var displayName = string.Equals(request.AccessToken, BingoSamplePlayers.Observer, StringComparison.Ordinal)
            ? "Observer"
            : request.AccessToken.Replace("player-", "Player ", StringComparison.Ordinal);
        return ValueTask.FromResult(new AuthenticatePlayerRes
        {
            Accepted = true,
            ActorId = request.AccessToken,
            DisplayName = displayName,
        });
    }
}
