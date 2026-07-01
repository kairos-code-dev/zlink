package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdD3RouteBridgeYieldScenario {
    private YdD3RouteBridgeYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runRouteBridgeYield(connector);
        System.out.println("scenario YD-D3 passed");
    }
}
