package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class ShutdownAwaitScenario {
    private ShutdownAwaitScenario() {
    }

    public static void runWait(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runShutdownWait(connector);
    }

    public static void runRecovery(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runShutdownRecovery(connector);
    }
}
