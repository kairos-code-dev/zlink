package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB2SameActorReentryScenario {
    private YdB2SameActorReentryScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runSameActorReentry(connector);
        System.out.println("scenario YD-B2 passed");
    }
}
