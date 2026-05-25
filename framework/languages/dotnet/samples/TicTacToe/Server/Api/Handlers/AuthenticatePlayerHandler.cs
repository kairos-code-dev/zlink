using Systems.Zlink.Codecs.Json;
using Microsoft.Extensions.Logging;
using TicTacToe.Server.Api;
using TicTacToe.Server.Api.Handlers;
using TicTacToe.Server.Configuration;
using TicTacToe.Server.Play;
using TicTacToe.Server.Play.EntrySpot;
using TicTacToe.Server.Play.GameSpots;
using TicTacToe.Server.Play.Sessions;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Configuration;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Zlink.Framework.Contracts.Streams;
using Zlink.Framework.Contracts.Timers;
using Zlink.Framework.Runtime.Core;

namespace TicTacToe.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
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
