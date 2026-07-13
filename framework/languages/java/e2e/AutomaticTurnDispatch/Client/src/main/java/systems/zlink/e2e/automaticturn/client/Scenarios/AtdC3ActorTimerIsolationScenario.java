package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdC3ActorTimerIsolationScenario {
    private AtdC3ActorTimerIsolationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runActorTimerIsolation(connector);
        System.out.println("scenario ATD-C3 passed");
    }
}
