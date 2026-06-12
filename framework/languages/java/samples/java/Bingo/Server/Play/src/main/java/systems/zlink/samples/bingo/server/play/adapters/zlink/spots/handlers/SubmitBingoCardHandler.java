package systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers;

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
    public Messages.SubmitBingoCardRes handle(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SubmitBingoCardReq request,
        CancellationToken cancellationToken) {
        return spot.submitCard(actor, request);
    }
}
