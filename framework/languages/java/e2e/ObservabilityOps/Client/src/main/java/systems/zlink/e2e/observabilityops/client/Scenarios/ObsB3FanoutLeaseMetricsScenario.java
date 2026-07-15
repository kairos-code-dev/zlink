package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.observabilityops.client.ObservabilityScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ObsB3FanoutLeaseMetricsScenario {
    private ObsB3FanoutLeaseMetricsScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        ObservabilityScenarioSupport.runTimerIsolation(connector);
    }
}
