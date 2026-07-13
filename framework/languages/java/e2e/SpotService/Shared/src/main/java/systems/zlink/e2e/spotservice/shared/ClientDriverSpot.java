package systems.zlink.e2e.spotservice.shared;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.SpotHandleResolver;

public final class ClientDriverSpot implements ZLinkSpot<ZLinkActor> {
    private static CompletableFuture<Void> result = new CompletableFuture<>();
    private static String mode = "state1";
    private final ZLinkSpotContext context;
    private final ZLinkRouteClient routes;
    private final ZLinkActorClient actors;
    private final ZLinkActorDirectory actorRefs;
    private final SpotHandleResolver spotHandles;

    @Override
    public CompletionStage<Void> onJoinedActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    @Override
    public CompletionStage<Void> onLeaveActor(ZLinkActor actor) {
        return CompletableFuture.completedFuture(null);
    }

    public ClientDriverSpot(
        ZLinkSpotContext context,
        ZLinkRouteClient routes,
        ZLinkActorClient actors,
        ZLinkActorDirectory actorRefs,
        SpotHandleResolver spotHandles) {
        this.context = context;
        this.routes = routes;
        this.actors = actors;
        this.actorRefs = actorRefs;
        this.spotHandles = spotHandles;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public CompletionStage<Void> onInitialize() {
        return context.runWorker(() -> {
            new ClientScenario(context.outbound(), routes, actors, actorRefs, spotHandles).runMode(mode);
            return (Void) null;
        }).submit().whenComplete((ignored, error) -> {
            if (error == null) {
                result.complete(null);
            } else {
                result.completeExceptionally(error);
            }
        });
    }

    public static void configure(String nextMode) {
        mode = nextMode;
        result = new CompletableFuture<>();
    }

    public static void awaitResult() {
        try {
            result.get(90, TimeUnit.SECONDS);
        } catch (Exception error) {
            throw new IllegalStateException("client driver scenario failed", error);
        }
    }
}
