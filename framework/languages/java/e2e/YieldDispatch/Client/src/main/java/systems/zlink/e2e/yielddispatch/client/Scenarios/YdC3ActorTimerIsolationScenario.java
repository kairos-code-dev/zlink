package systems.zlink.e2e.yielddispatch.client.Scenarios;

import systems.zlink.e2e.yielddispatch.client.Support.YieldDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdC3ActorTimerIsolationScenario {
    private YdC3ActorTimerIsolationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        YieldDispatchScenarioSupport.runActorTimerIsolation(connector);
        System.out.println("scenario YD-C3 passed");
    }
}
