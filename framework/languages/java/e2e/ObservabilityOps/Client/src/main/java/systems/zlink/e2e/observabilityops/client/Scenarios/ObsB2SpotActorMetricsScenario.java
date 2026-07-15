package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.observabilityops.client.ObservabilityScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ObsB2SpotActorMetricsScenario {
    private ObsB2SpotActorMetricsScenario() {
    }

    public static void transfer(ZLinkStreamConnector connector) throws Exception {
        ObservabilityScenarioSupport.runObservabilityTransfer(connector);
    }

    public static void queue(ZLinkStreamConnector connector) throws Exception {
        ObservabilityScenarioSupport.runObservabilityQueue(connector);
    }
}
