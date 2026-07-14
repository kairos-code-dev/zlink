package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.entryspot.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.tictactoe.server.configuration.SampleNames;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.entryspot.PlayEntrySpot;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinReq;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinRes;

@ZLinkHandlerGroup(SampleNames.PlayActor)
public final class PlayActorJoinGameHandler {
    @ZLinkSpotActorRequest
    public java.util.concurrent.CompletionStage<JoinGameRes> joinGame(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorRequestContext context,
        JoinGameReq request) {
        return actor.context()
            .joinSpot(RoutingId.from(request.roomId()),
                new TicTacToeGameJoinReq(request.roomId(), actor.requirePlayer()))
            .submit(TicTacToeGameJoinRes.class)
            .thenApply(joined -> {
                if (!(joined instanceof ZLinkActorJoinResult.Accepted<?> accepted)) {
                    throw new IllegalStateException("Tic-tac-toe room join was rejected");
                }
                TicTacToeGameJoinRes reply = (TicTacToeGameJoinRes) accepted.reply();
                actor.joinGame(request.roomId());
                return new JoinGameRes(reply.state());
            });
    }
}
