package systems.zlink.e2e.automaticturn.client.Scenarios;

import systems.zlink.e2e.automaticturn.client.Support.AutomaticTurnDispatchScenarioSupport;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class AtdA4WorkerAwaitScenario {
    private AtdA4WorkerAwaitScenario() {
    }

    public static void run(ZLinkStreamConnector connector) throws Exception {
        AutomaticTurnDispatchScenarioSupport.runWorkerAwait(connector);
        System.out.println("scenario ATD-A4 passed");
    }
}
