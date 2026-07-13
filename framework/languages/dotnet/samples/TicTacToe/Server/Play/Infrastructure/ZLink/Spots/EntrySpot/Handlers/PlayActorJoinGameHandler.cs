using Systems.Zlink;
using TicTacToe.Server.Play.Infrastructure.ZLink.Actors;
using TicTacToe.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Infrastructure.ZLink.Spots.EntrySpot.Handlers;

[ZLinkSpotActorRequestHandler(nameof(JoinGameReq))]
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
        logger.LogInformation(
            "actor: JoinGameReq received. actor={ActorId}, roomId={RoomId}",
            actor.ActorId,
            message.RoomId);

        var spotRid = RoutingId.From(message.RoomId);
        var joined = await actor.Context.JoinSpot(
                spotRid,
                new TicTacToeGameJoinReq(message.RoomId, actor.RequirePlayer()))
            .Async(cancellationToken);

        var joinMessage = joined switch
        {
            ZLinkActorJoinResult.Accepted accepted => accepted.Reply,
            ZLinkActorJoinResult.Rejected rejected => rejected.Reply,
            _ => throw new InvalidOperationException("Unknown actor join result.")
        };
        var joinReply = joinMessage.Decode<TicTacToeGameJoinRes>();
        var reply = new JoinGameRes(joinReply.State);
        logger.LogInformation(
            "actor -> client: JoinGameRes returned. actor={ActorId}, roomId={RoomId}, mark={Mark}",
            actor.ActorId,
            reply.State.RoomId,
            reply.State.XActorId == actor.ActorId ? TicTacToeMarks.X : TicTacToeMarks.O);
        return reply;
    }
}
