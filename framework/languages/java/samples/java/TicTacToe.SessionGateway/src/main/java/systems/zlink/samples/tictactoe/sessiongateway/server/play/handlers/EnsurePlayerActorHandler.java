package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

public final class EnsurePlayerActorHandler implements ZLinkRequestHandler<String, String> {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public CompletionStage<String> handleAsync(String actorId, ZLinkRequestContext context) {
        return actors.getOrCreateAsync(actorId, SampleNames.PlayerActorType)
            .thenCompose(actor -> actor.context()
                .joinEntrySpot(RoutingId.from(SampleNames.EntrySpotRoutingId))
                .submitAsync()
                .thenApply(joined -> snapshot(actor, joined)));
    }

    private static String snapshot(ZLinkActor actor, systems.zlink.framework.actors.ZLinkActorRef ref) {
        return ref.nodeRid().toHex() + "|" + actor.actorId() + "|" + ref.epoch();
    }
}
