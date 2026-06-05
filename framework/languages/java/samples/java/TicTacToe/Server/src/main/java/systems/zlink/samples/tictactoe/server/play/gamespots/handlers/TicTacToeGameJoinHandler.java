package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.framework.CancellationToken;
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
        TicTacToeGame spot,
        PlayActor actor,
        TicTacToeGameJoinReq request,
        CancellationToken cancellationToken) {
        return spot.join(actor, request.gameId());
    }
}
