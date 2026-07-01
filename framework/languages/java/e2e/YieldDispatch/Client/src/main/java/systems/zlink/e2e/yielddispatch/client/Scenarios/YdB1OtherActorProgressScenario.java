package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB1OtherActorProgressScenario {
    private YdB1OtherActorProgressScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runActorOtherProgress(connector);
        System.out.println("scenario YD-B1 passed");
    }
}
