package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.observabilityops.client.ObservabilityScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ObsC2ActorHandoffScenario {
    private ObsC2ActorHandoffScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        ObservabilityScenarioSupport.runObservabilityDrainHandoff(connector);
    }
}
