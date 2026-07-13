package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdB1OtherActorProgressScenario {
    private AtdB1OtherActorProgressScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runActorOtherProgress(connector);
        System.out.println("scenario ATD-B1 passed");
    }
}
