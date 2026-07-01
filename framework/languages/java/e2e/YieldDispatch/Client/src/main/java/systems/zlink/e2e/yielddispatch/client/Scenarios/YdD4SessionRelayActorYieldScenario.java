package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdD4SessionRelayActorYieldScenario {
    private YdD4SessionRelayActorYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runSessionRelayActorYield(connector);
        System.out.println("scenario YD-D4 passed");
    }
}
