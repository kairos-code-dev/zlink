package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA2YieldTerminatorScenario {
    private YdA2YieldTerminatorScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runYieldTerminator(connector);
        System.out.println("scenario YD-A2 passed");
    }
}
