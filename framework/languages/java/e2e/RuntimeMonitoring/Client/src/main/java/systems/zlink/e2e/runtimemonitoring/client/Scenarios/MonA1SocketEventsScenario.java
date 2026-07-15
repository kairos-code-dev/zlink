package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonA1SocketEventsScenario {
    private MonA1SocketEventsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        context.request("a1-0");
        context.waitForAnyEvent(context.serviceEndpoint(), "socket", Set.of(
            "CONNECTED", "CONNECTION_READY"));
        System.out.println("scenario MON-A1 passed");
    }
}
