package systems.zlink.e2e.monitoring;

import java.time.Duration;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.framework.spots.ZLinkSpotTimerHandler;
import systems.zlink.framework.spots.ZLinkTimerOptions;
import systems.zlink.framework.spots.ZLinkTimerTick;

public final class MonitoringSpot implements ZLinkSpot<ZLinkActor> {
    private final ZLinkSpotContext context;

    public MonitoringSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    @Override
    public ZLinkSpotCreateResponse onCreate(ZLinkMessage request) {
        ZLinkTimerOptions options = new ZLinkTimerOptions();
        options.setStopOnUnhandledException(false);
        context.addTimer("failing-monitoring-timer", Duration.ofMillis(100),
            FailingTimerHandler.class, options);
        return ZLinkSpotCreateResponse.accept();
    }

    public static final class FailingTimerHandler
        implements ZLinkSpotTimerHandler<MonitoringSpot> {
        @Override
        public void handle(MonitoringSpot spot, ZLinkTimerTick tick) {
            throw new IllegalStateException("monitoring timer boom");
        }
    }
}
