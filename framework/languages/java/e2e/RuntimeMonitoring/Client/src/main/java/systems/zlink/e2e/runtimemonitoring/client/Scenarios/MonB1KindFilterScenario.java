package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;

public final class MonB1KindFilterScenario {
    private MonB1KindFilterScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        Set<String> observed = context.events(
            context.serviceEndpoint(), "socket", Contracts.CHANNEL);
        MonitoringScenarioContext.ensure(observed.contains("CONNECTION_READY"),
            "MON-B1 did not observe filtered CONNECTION_READY event");
        MonitoringScenarioContext.ensure(observed.equals(Set.of("CONNECTION_READY")),
            "MON-B1 socket filter allowed unexpected events: " + observed);
        System.out.println("scenario MON-B1 passed");
    }
}
