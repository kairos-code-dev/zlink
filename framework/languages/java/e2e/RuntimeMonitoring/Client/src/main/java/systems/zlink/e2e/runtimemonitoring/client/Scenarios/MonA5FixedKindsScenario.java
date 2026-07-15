package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;

public final class MonA5FixedKindsScenario {
    private MonA5FixedKindsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        context.triggerHandshakeFailure();
        context.waitForEvent(context.serviceEndpoint(), "location", Set.of("STATUS_CHANGED"));
        context.waitForEvent(context.serviceEndpoint(), "spot", Set.of(
            "STATUS_CHANGED", "TIMER_STOPPED_AFTER_UNHANDLED_EXCEPTION"));
        context.waitForEvent(
            context.serviceEndpoint(), "socket", Contracts.HANDSHAKE_CHANNEL, Set.of("DISCONNECTED"));
        System.out.println("scenario MON-A5 passed");
    }
}
