package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdD3RouteBridgeAwaitScenario {
    private AtdD3RouteBridgeAwaitScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runRouteBridgeAwait(connector);
        System.out.println("scenario ATD-D3 passed");
    }
}
