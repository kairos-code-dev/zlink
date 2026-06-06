package systems.zlink.samples.bingo.server.play.bingoroomspots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.bingo.server.play.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.bingoroomspots.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class StartBingoGameHandler
    implements ZLinkSpotActorRequestHandler<
        BingoRoomSpot,
        PlayerActor,
        Messages.StartBingoGameReq,
        Messages.StartBingoGameRes> {
    @Override
    public CompletionStage<Messages.StartBingoGameRes> handleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.StartBingoGameReq request,
        CancellationToken cancellationToken) {
        return spot.startAsync(actor, request);
    }
}
