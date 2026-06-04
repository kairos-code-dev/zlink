package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkSpotActorRequest;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class StartBingoGameHandler {
    @ZLinkSpotActorRequest
    public CompletionStage<Messages.StartBingoGameRes> handleAsync(
        PlayerActor actor,
        Messages.StartBingoGameReq request) {
        return actor.context()
            .getSpot(BingoRoomSpot.class)
            .startAsync(actor, request);
    }
}
