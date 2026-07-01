package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA4WorkerYieldScenario {
    private YdA4WorkerYieldScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runWorkerYield(connector);
        System.out.println("scenario YD-A4 passed");
    }
}
