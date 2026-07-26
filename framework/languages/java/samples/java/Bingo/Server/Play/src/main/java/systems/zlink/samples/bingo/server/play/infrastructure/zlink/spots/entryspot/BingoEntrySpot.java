package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot;


import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkActorCreateResponse;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.bingoroomspot.BingoRoomSpot;
import systems.zlink.samples.bingo.shared.contracts.BingoMessages;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class BingoEntrySpot implements ZLinkEntrySpot<PlayerActor> {
    private final ZLinkEntrySpotContext context;
    private final ZLinkSpotManager spots;
    private final ObjectMapper json;

    public BingoEntrySpot(
        ZLinkEntrySpotContext context,
        ZLinkSpotManager spots,
        ObjectMapper json) {
        this.context = context;
        this.spots = spots;
        this.json = json;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkActorCreateResponse> onCreateActor(
        PlayerActor actor,
        ZLinkMessage createRequest) {
        Messages.EnsurePlayerActorReq request =
            createRequest.decode(Messages.EnsurePlayerActorReq.class);
        actor.setDisplayName(request.getDisplayName());
        return java.util.concurrent.CompletableFuture.completedFuture(
            ZLinkActorCreateResponse.accept());
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onJoinedActor(
        PlayerActor actor) {
        if (actor.destroyAfterEntrySpotJoin()) {
            return context.destroyActor(actor);
        }
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onLeaveActor(
        PlayerActor actor) {
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<Void> onDisconnectActor(
        PlayerActor actor) {
        actor.markDisconnected();
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    public java.util.concurrent.CompletionStage<Messages.ObserveBingoEventsRes> observeEvents(
        PlayerActor actor,
        Messages.ObserveBingoEventsReq request) {
        String observerSpotId = "observe:" + request.getRoomId() + ":" + actor.actorId();
        BingoRoomModels.BingoRoomSettings settings =
            BingoRoomModels.BingoRoomSettings.createObserver(
                request.getRoomId(),
                actor.actorId(),
                SampleTimings.DrawPeriod.toMillis());
        return spots.getOrCreate(observerSpotId, BingoRoomSpot.class.getName())
            .request(settings)
            .submit()
            .thenApply(ignored -> {
                actor.context().joinSpot(
                    observerSpotId,
                    BingoMessages.bingoRoomJoinReq(
                        request.getRoomId(),
                        actor.actorId(),
                        actor.displayName(),
                        true))
                    .defer();
                return BingoMessages.observeBingoEventsRes(true);
            });
    }

}
