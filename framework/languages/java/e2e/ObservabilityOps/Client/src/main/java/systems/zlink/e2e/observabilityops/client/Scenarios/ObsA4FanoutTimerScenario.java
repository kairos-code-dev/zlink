package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.observabilityops.client.ObservabilityScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ObsA4FanoutTimerScenario {
    private ObsA4FanoutTimerScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        ObservabilityScenarioSupport.runTimerIsolation(connector);
    }
}
