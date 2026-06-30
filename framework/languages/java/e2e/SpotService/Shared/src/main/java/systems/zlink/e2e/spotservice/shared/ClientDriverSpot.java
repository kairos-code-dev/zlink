package systems.zlink.e2e.spotservice.shared;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class ClientDriverSpot implements ZLinkSpot<ZLinkActor> {
    private static CompletableFuture<Void> result = new CompletableFuture<>();
    private static String mode = "state1";
    private final ZLinkSpotContext context;
    private final ZLinkRouteClient routes;

    public ClientDriverSpot(
        ZLinkSpotContext context,
        ZLinkRouteClient routes) {
        this.context = context;
        this.routes = routes;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public void onInitialize() {
        try {
            new ClientScenario(context.outbound(), routes).runMode(mode);
            result.complete(null);
        } catch (Throwable error) {
            result.completeExceptionally(error);
        }
    }

    public static void configure(String nextMode) {
        mode = nextMode;
        result = new CompletableFuture<>();
    }

    public static void awaitResult() {
        try {
            result.get(45, TimeUnit.SECONDS);
        } catch (Exception error) {
            throw new IllegalStateException("client driver scenario failed", error);
        }
    }
}
