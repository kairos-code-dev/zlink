package systems.zlink.framework.spots;

import java.time.Duration;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;

public interface ZLinkSpotContext {
    RoutingId spotRid();

    RoutingId nodeRid();

    ZLinkSpotOutbound outbound();

    CompletionStage<Void> leaveActorAsync(ZLinkActor actor);

    CompletionStage<ZLinkTimer> addTimer(
        String name,
        Duration period,
        Class<?> handlerType,
        ZLinkTimerOptions options);
}
