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

internal sealed class PlayActorPlaceMarkHandler(ILogger<PlayActorPlaceMarkHandler> logger)
{
    [ZLinkSpotActorRequest]
    public async ValueTask<PlaceMarkRes> HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        PlaceMarkReq message,
        CancellationToken cancellationToken)
    {
        var gameId = actor.RequireJoinedGame();
        logger.LogInformation(
            "actor: PlaceMarkReq received. actor={ActorId}, gameId={GameId}, cell={Cell}",
            actor.ActorId,
            gameId,
            message.Cell);

        var reply = await spot.PlaceMarkAsync(actor, message.Cell, cancellationToken);

        logger.LogInformation(
            "actor -> client: PlaceMarkRes returned. actor={ActorId}, gameId={GameId}, board={Board}, status={Status}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.Board,
            reply.State.Status);
        return reply;
    }
}
