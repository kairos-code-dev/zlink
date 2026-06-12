package systems.zlink.framework.spots;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkEntrySpotContext {
    RoutingId spotRid();

    RoutingId nodeRid();

    default ZLinkSpotHandlerRegistry handlers() {
        throw new UnsupportedOperationException(
            "SPOT handler registration is only available on runtime-created contexts");
    }

    ZLinkSpotOutbound outbound();

    CompletionStage<Void> destroyActorAsync(ZLinkActor actor);

    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options);
}
