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
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.GameSpots.Handlers;

sealed class TicTacToeGameJoinHandler(ILogger<TicTacToeGameJoinHandler> logger)
{
    [ZLinkSpotActorJoin]
    public async ValueTask<TicTacToeGameJoinRes> HandleAsync(
        TicTacToeGame spot,
        PlayActor player,
        TicTacToeGameJoinReq request,
        CancellationToken cancellationToken)
    {
        var reply = await spot.JoinPlayerAsync(player, request.GameId, cancellationToken);
        logger.LogInformation(
            "TicTacToeGame: actor join accepted. actor={ActorId}, gameId={GameId}, mark={Mark}",
            player.ActorId,
            request.GameId,
            reply.State.XActorId == player.ActorId ? "X" : "O");

        return reply;
    }
}
