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

internal sealed class TicTacToeGameActorJoinedHandler(ILogger<TicTacToeGameActorJoinedHandler> logger)
{
    [ZLinkSpotPostActorJoined]
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, gameId={GameId}, kind={Kind}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
