package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdD4SessionRelayActorAwaitScenario {
    private AtdD4SessionRelayActorAwaitScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runSessionRelayActorAwait(connector);
        System.out.println("scenario ATD-D4 passed");
    }
}
