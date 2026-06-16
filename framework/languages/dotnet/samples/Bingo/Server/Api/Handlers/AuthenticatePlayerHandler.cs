using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
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
        if (!request.AccessToken.StartsWith("player-", StringComparison.Ordinal))
        {
            return ValueTask.FromResult(new AuthenticatePlayerRes
            {
                Accepted = false,
                Reason = "Access token must be a sample player id.",
            });
        }

        var displayName = request.AccessToken.Replace("player-", "Player ", StringComparison.Ordinal);
        return ValueTask.FromResult(new AuthenticatePlayerRes
        {
            Accepted = true,
            ActorId = request.AccessToken,
            DisplayName = displayName,
        });
    }
}
