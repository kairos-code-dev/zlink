package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.observabilityops.client.ObservabilityScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ObsC5RolloutScenario {
    private ObsC5RolloutScenario() {
    }

    public static void bind(ZLinkStreamConnector connector) {
        ObservabilityScenarioSupport.runDrainRolloutBind(connector);
    }

    public static void probe(ZLinkStreamConnector connector) {
        ObservabilityScenarioSupport.runDrainTargetProbe(connector);
    }
}
