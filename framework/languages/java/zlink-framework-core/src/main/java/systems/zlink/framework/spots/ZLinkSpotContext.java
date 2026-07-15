package systems.zlink.framework.spots;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkSpotContext {
    RoutingId spotRid();

    RoutingId nodeRid();

    default ZLinkSpotHandlerRegistry handlers() {
        throw new UnsupportedOperationException(
            "SPOT handler registration is only available on runtime-created contexts");
    }

    ZLinkSpotOutbound outbound();

    /**
     * Offloads work to the framework worker pool. The work runs outside this Spot's
     * serial execution line; completion callbacks and awaitable continuations
     * re-enter the Spot serial line.
     */
    default <T> ZLinkWorkerCall<T> runWorker(ZLinkWorkerTask<T> work) {
        throw new UnsupportedOperationException(
            "worker offload is only available on runtime-created contexts");
    }

    CompletionStage<Void> leaveActor(ZLinkActor actor);

    CompletionStage<Boolean> close();

    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options);
}
