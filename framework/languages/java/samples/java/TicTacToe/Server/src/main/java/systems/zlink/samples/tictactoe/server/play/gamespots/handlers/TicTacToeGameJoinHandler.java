package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinReq;
import systems.zlink.samples.tictactoe.shared.contracts.TicTacToeGameJoinRes;

@ZLinkHandlerGroup("play-actor")
public final class TicTacToeGameJoinHandler {
    @ZLinkSpotActorJoin
    public java.util.concurrent.CompletionStage<TicTacToeGameJoinRes> join(
        PlayActor actor,
        TicTacToeGameJoinReq request) {
        return actor.context()
            .getSpot(TicTacToeGame.class)
            .join(actor, request.gameId());
    }
}
