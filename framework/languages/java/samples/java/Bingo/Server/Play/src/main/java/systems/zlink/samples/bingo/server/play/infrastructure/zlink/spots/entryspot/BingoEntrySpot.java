package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots.entryspot;


import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
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
    public java.util.concurrent.CompletionStage<Void> onCreateActor(
        PlayerActor actor,
        ZLinkMessage createRequest) {
        Messages.EnsurePlayerActorReq request =
            createRequest.decode(Messages.EnsurePlayerActorReq.class);
        actor.setDisplayName(request.getDisplayName());
        return java.util.concurrent.CompletableFuture.completedFuture(null);
    }

    @Override
    public java.util.concurrent.CompletionStage<ZLinkSpotActorJoinResponse> onActorJoin(
        String actorId,
        ZLinkMessage request) {
        return java.util.concurrent.CompletableFuture.completedFuture(ZLinkSpotActorJoinResponse.accept());
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
        String observerRid = "observe:" + request.getRoomId() + ":" + context.nodeRid();
        BingoRoomModels.BingoRoomSettings settings =
            BingoRoomModels.BingoRoomSettings.createObserver(
                request.getRoomId(),
                context.nodeRid().toString(),
                SampleTimings.DrawPeriod.toMillis());
        return spots.getOrCreate(BingoRoomSpot.class, RoutingId.from(observerRid), ZLinkMessage.of(settings))
            .thenCompose(ignored -> actor.context().joinSpot(
                    RoutingId.from(observerRid),
                    BingoMessages.bingoRoomJoinReq(
                        request.getRoomId(),
                        actor.actorId(),
                        actor.displayName(),
                        true))
                .submit(Messages.BingoRoomJoinRes.class))
            .thenApply(joined -> BingoMessages.observeBingoEventsRes(
                joined.reply().getState().getStatus().equals("Running"),
                joinedActor(joined).nodeRid().toString()));
    }

    private static systems.zlink.framework.actors.ActorRef joinedActor(
        systems.zlink.framework.actors.ZLinkActorJoinResult<Messages.BingoRoomJoinRes> joined) {
        if (joined instanceof systems.zlink.framework.actors.ZLinkActorJoinResult.Accepted<Messages.BingoRoomJoinRes>
            accepted) {
            return accepted.actor();
        }
        throw new IllegalStateException("observer actor join was rejected");
    }

}
