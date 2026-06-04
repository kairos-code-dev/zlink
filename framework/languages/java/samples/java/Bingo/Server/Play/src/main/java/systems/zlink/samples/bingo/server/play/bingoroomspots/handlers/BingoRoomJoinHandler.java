package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorJoin;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoRoomJoinHandler {
    @ZLinkSpotActorJoin
    public CompletionStage<Messages.BingoRoomJoinRes> handleAsync(
        PlayerActor actor,
        Messages.BingoRoomJoinReq request) {
        return actor.context()
            .getSpot(BingoRoomSpot.class)
            .joinAsync(actor, request);
    }
}
