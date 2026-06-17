package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor;

public final class PlayEntrySpot implements ZLinkEntrySpot<PlayActor> {
    private final ZLinkEntrySpotContext context;

    public PlayEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void onCreateActor(
        PlayActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        PlayActor actor,
        Message request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept(Message.from(new byte[0]));
    }

    @Override
    public void onJoinedActor(
        PlayActor actor,
        CancellationToken cancellationToken) {
        if (actor.destroyAfterEntrySpotJoin()) {
            await(context.destroyActor(actor));
        }
    }

    @Override
    public void onDisconnectActor(
        PlayActor actor,
        CancellationToken cancellationToken) {
        actor.markDisconnected();
    }
}
