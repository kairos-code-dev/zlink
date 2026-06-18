using Zlink.Framework.Contracts.Codecs.Json;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using Systems.Zlink;
using TicTacToe.Server.Play.Adapters.ZLink.Actors;

namespace TicTacToe.Server.Play.Adapters.ZLink.Spots.Handlers;

internal sealed class PlayActorJoinGameHandler(ILogger<PlayActorJoinGameHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<PlayEntrySpot, PlayActor, JoinGameReq, JoinGameRes>
{
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
            "actor: JoinGameReq received. actor={ActorId}, roomId={RoomId}",
            actor.ActorId,
            message.RoomId);

        var spotRid = RoutingId.From(message.RoomId);
        var joined = await actor.Context.JoinSpot(
                spotRid,
                new TicTacToeGameJoinReq(message.RoomId, actor.ActorId).ToJson())
            .Async(cancellationToken);

        var joinReply = joined.Reply.FromJson<TicTacToeGameJoinRes>();
        var reply = new JoinGameRes(joinReply.State);
        logger.LogInformation(
            "actor -> client: JoinGameRes returned. actor={ActorId}, roomId={RoomId}, mark={Mark}",
            actor.ActorId,
            reply.State.RoomId,
            reply.State.XActorId == actor.ActorId ? TicTacToeMarks.X : TicTacToeMarks.O);
        return reply;
    }
}
