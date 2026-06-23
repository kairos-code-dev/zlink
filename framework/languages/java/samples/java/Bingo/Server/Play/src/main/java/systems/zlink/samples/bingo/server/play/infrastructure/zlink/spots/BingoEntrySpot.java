package systems.zlink.samples.bingo.server.play.infrastructure.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import com.fasterxml.jackson.databind.ObjectMapper;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.framework.spots.ZLinkSpotManager;
import systems.zlink.samples.bingo.server.configuration.SampleNames;
import systems.zlink.samples.bingo.server.configuration.SampleTimings;
import systems.zlink.samples.bingo.server.play.infrastructure.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.domain.bingo.BingoRoomModels;
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
    public void configure() {
    }

    @Override
    public void onCreateActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        PlayerActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
        if (actor.destroyAfterEntrySpotJoin()) {
            await(context.destroyActor(actor));
        }
    }

    @Override
    public void onLeaveActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(
        PlayerActor actor,
        CancellationToken cancellationToken) {
        actor.markDisconnected();
    }

    public Messages.ObserveBingoEventsRes observeEvents(
        PlayerActor actor,
        Messages.ObserveBingoEventsReq request) {
        String observerRid = "observe:" + request.roomId() + ":" + context.nodeRid();
        BingoRoomModels.BingoRoomSettings settings =
            BingoRoomModels.BingoRoomSettings.createObserver(
                request.roomId(),
                context.nodeRid().toString(),
                SampleTimings.DrawPeriod.toMillis());
        await(spots.getOrCreate(BingoRoomSpot.class, RoutingId.from(observerRid), settings));
        var joined = actor.context()
            .joinSpot(
                RoutingId.from(observerRid),
                new Messages.BingoRoomJoinReq(
                    request.roomId(),
                    actor.actorId(),
                    actor.displayName(),
                    true))
            .await(Messages.BingoRoomJoinRes.class);
        return new Messages.ObserveBingoEventsRes(
            joined.reply().state().status().equals("Running"),
            joined.actor().nodeRid().toString());
    }

}
