package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.samples.tictactoe.server.play.actors.PlayActor;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameReq;
import systems.zlink.samples.tictactoe.shared.contracts.JoinGameRes;

@ZLinkHandlerGroup("play-actor")
public final class TicTacToeGameJoinHandler {
    public JoinGameRes join(String gameId, String actorId) {
        return TicTacToeGameDirectory.get(gameId).join(actorId);
    }

    @ZLinkSpotActorJoin
    public JoinGameRes join(
        PlayActor actor,
        JoinGameReq request) {
        JoinGameRes reply = TicTacToeGameDirectory.get(request.gameId())
            .join(actor.actorId());
        actor.joinGame(request.gameId());
        return reply;
    }
}
