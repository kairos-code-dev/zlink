package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.Collection;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdD1LocalTopologyScenario {
    private static final String[] REQUIRED_LOCAL_SCENARIOS = {
        "YD-A1",
        "YD-A2",
        "YD-A3",
        "YD-A4",
        "YD-B1",
        "YD-B2",
        "YD-B3",
        "YD-C1",
        "YD-C2",
        "YD-C3",
        "YD-D4",
        "YD-E1"
    };

    private YdD1LocalTopologyScenario() {
    }

    public static void run(ZLinkStreamConnector connector, Collection<String> completedScenarios) {
        if (connector == null) {
            throw new IllegalArgumentException("connector is required");
        }
        for (String scenario : REQUIRED_LOCAL_SCENARIOS) {
            if (!completedScenarios.contains(scenario)) {
                throw new IllegalStateException("YD-D1 missing local topology scenario marker: " + scenario);
            }
        }
        System.out.println("scenario YD-D1 passed");
    }
}
