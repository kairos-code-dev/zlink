package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdD2RemoteSpotYieldScenario {
    private YdD2RemoteSpotYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runRemoteSpotYield(connector);
        System.out.println("scenario YD-D2 passed");
    }
}
