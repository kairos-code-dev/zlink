package systems.zlink.e2e.observabilityops.client.Scenarios;

import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.observabilityops.trigger.TriggerOperations;

public final class ObsA2ErrorFlowScenario {
    private ObsA2ErrorFlowScenario() {
    }

    public static void run() throws Exception {
        TriggerOperations.runMissingHandler(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"));
    }
}
