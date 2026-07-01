package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ShutdownYieldScenario {
    private ShutdownYieldScenario() {
    }

    public static void runWait(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runShutdownWait(connector);
    }

    public static void runRecovery(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runShutdownRecovery(connector);
    }
}
