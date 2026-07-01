package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA1BasicTerminatorScenario {
    private YdA1BasicTerminatorScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runBasicTerminator(connector);
        System.out.println("scenario YD-A1 passed");
    }
}
