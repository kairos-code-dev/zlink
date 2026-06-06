package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorJoinHandler;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoRoomJoinHandler
    implements ZLinkSpotActorJoinHandler<
        BingoRoomSpot,
        PlayerActor,
        Messages.BingoRoomJoinReq,
        Messages.BingoRoomJoinRes> {
    @Override
    public CompletionStage<Messages.BingoRoomJoinRes> handleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        Messages.BingoRoomJoinReq request,
        CancellationToken cancellationToken) {
        return spot.joinAsync(actor, request);
    }
}
