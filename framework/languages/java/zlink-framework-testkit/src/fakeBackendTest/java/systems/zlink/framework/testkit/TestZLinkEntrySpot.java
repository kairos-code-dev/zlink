package systems.zlink.framework.testkit;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public abstract class TestZLinkEntrySpot<TActor extends ZLinkActor>
    implements ZLinkEntrySpot<TActor> {
    @Override
    public CompletionStage<Void> onJoinedActor(TActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(TActor actor) {
        return CompletableFuture.completedFuture(null);
    }
}
