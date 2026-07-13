package systems.zlink.framework.spring;

import org.springframework.boot.actuate.health.Health;
import org.springframework.boot.actuate.health.HealthIndicator;
import systems.zlink.framework.monitoring.ZLinkDrainControl;

public final class ZLinkDrainReadinessContributor implements HealthIndicator {
    private final ZLinkDrainControl drainControl;

    public ZLinkDrainReadinessContributor(ZLinkDrainControl drainControl) {
        this.drainControl = java.util.Objects.requireNonNull(drainControl, "drainControl");
    }

    @Override
    public Health health() {
        return drainControl.isReady()
            ? Health.up().build()
            : Health.outOfService().withDetail("state", "draining").build();
    }
}
