package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdE2CancellationScenario {
    private AtdE2CancellationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runCancellationCleanup(connector);
        System.out.println("scenario ATD-E2 passed");
    }
}
