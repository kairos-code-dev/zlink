package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdA1BasicTerminatorScenario {
    private AtdA1BasicTerminatorScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runBasicTerminator(connector);
        System.out.println("scenario ATD-A1 passed");
    }
}
