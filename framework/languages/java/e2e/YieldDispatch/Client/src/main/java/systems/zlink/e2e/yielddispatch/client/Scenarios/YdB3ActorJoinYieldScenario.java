package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB3ActorJoinYieldScenario {
    private YdB3ActorJoinYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runActorJoinYield(connector);
        System.out.println("scenario YD-B3 passed");
    }
}
