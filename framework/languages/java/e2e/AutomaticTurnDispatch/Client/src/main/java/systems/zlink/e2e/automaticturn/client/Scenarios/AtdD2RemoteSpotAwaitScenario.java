package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdD2RemoteSpotAwaitScenario {
    private AtdD2RemoteSpotAwaitScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runRemoteSpotAwait(connector);
        System.out.println("scenario ATD-D2 passed");
    }
}
