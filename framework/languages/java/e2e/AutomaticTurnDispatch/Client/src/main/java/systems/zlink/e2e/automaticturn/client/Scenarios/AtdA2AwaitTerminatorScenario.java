package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdA2AwaitTerminatorScenario {
    private AtdA2AwaitTerminatorScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runAwaitTerminator(connector);
        System.out.println("scenario ATD-A2 passed");
    }
}
