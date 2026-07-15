package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.observabilityops.client.ObservabilityScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ObsC3SpotDrainPolicyScenario {
    private ObsC3SpotDrainPolicyScenario() {
    }

    public static void write(ZLinkStreamConnector connector) {
        ObservabilityScenarioSupport.runPersistentRoomWrite(connector);
    }

    public static void read(ZLinkStreamConnector connector) {
        ObservabilityScenarioSupport.runPersistentRoomRead(connector);
    }
}
