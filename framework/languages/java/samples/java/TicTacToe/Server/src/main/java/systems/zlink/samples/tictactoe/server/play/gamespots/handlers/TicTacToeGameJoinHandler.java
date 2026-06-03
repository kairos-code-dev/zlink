package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

@ZLinkHandlerGroup("play-actor")
public final class TicTacToeGameJoinHandler {
    @ZLinkSpotActorJoin
    public JoinGameRes join(
        PlayActor actor,
        JoinGameReq request) {
        return actor.context()
            .getSpot(TicTacToeGame.class)
            .join(actor.actorId());
    }
}
