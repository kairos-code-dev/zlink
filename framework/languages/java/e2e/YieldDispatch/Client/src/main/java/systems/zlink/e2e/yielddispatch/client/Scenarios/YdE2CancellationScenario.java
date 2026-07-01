package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdE2CancellationScenario {
    private YdE2CancellationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runCancellationCleanup(connector);
        System.out.println("scenario YD-E2 passed");
    }
}
