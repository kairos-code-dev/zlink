package systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class StopObservingBingoEventsHandler
    implements ZLinkSpotActorRequestHandler<
        BingoRoomSpot,
        PlayerActor,
        Messages.StopObservingBingoEventsReq,
        Messages.StopObservingBingoEventsRes> {
    @Override
    public Messages.StopObservingBingoEventsRes handle(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.StopObservingBingoEventsReq request,
        CancellationToken cancellationToken) {
        return spot.stopObserving(actor, request);
    }
}
