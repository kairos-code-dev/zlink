package systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class SubmitBingoCardHandler
    implements ZLinkSpotActorRequestHandler<
        BingoRoomSpot,
        PlayerActor,
        Messages.SubmitBingoCardReq,
        Messages.SubmitBingoCardRes> {
    @Override
    public CompletionStage<Messages.SubmitBingoCardRes> handleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SubmitBingoCardReq request,
        CancellationToken cancellationToken) {
        return spot.submitCardAsync(actor, request);
    }
}
