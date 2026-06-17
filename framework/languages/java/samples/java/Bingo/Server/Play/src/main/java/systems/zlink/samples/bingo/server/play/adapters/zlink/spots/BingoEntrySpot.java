package systems.zlink.samples.bingo.server.play.adapters.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;

public final class BingoEntrySpot implements ZLinkEntrySpot<PlayerActor> {
    private final ZLinkEntrySpotContext context;

    public BingoEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
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
        Message request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept(Message.from(new byte[0]));
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
}
