package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdB3ActorJoinAwaitScenario {
    private AtdB3ActorJoinAwaitScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runActorJoinAwait(connector);
        System.out.println("scenario ATD-B3 passed");
    }
}
