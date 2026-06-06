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
using Zlink.Framework.Runtime.Actors;
using Zlink.Framework.Runtime.Diagnostics;
using Zlink.Framework.Runtime.Execution;
using Zlink.Framework.Runtime.Host;
using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Registry;
using Systems.Zlink;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.EntrySpot.Handlers;

internal sealed class PlayActorJoinGameHandler(ILogger<PlayActorJoinGameHandler> logger)
{
    [ZLinkSpotActorRequest]
    public async ValueTask<JoinGameRes> HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        JoinGameReq message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = context;
        logger.LogInformation(
            "actor: JoinGameReq received. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            message.GameId);

        var spotRid = RoutingId.FromHex(message.GameId);
        var joined = await actor.Context.JoinSpot(
                spotRid,
                new TicTacToeGameJoinReq(message.GameId, actor.ActorId).Encode())
            .Timeout(SampleTimeouts.Request)
            .SubmitAsync(cancellationToken);

        var joinReply = joined.Reply.Decode<TicTacToeGameJoinRes>();
        var reply = new JoinGameRes(joinReply.State);
        logger.LogInformation(
            "actor -> client: JoinGameRes returned. actor={ActorId}, gameId={GameId}, mark={Mark}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.XActorId == actor.ActorId ? "X" : "O");
        return reply;
    }
}
