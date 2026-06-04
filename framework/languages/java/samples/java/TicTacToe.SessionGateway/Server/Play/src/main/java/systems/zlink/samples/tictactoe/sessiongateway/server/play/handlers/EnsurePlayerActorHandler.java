package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.sessiongateway.shared.configuration.SampleNames;

@ZLinkHandlerGroup("play")
public final class EnsurePlayerActorHandler {
    private final ZLinkActorManager actors;

    public EnsurePlayerActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @ZLinkRequest(packetName = "EnsurePlayerActor")
    public CompletionStage<String> handleAsync(String actorId) {
        return actors.getOrCreateAsync(actorId, SampleNames.PlayerActorType)
            .thenCompose(actor -> actor.context()
                .joinEntrySpot(RoutingId.from(SampleNames.PlayRid))
                .submitAsync()
                .thenApply(joined -> snapshot(actor, joined)));
    }

    private static String snapshot(ZLinkActor actor, systems.zlink.framework.actors.ZLinkActorRef ref) {
        return ref.nodeRid().toHex() + "|" + actor.actorId() + "|" + ref.epoch();
    }
}
