package systems.zlink.e2e.observabilityops.client.Scenarios;

import java.nio.file.Path;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.observabilityops.trigger.TriggerOperations;

public final class ObsB1ConnectionMetricsScenario {
    private ObsB1ConnectionMetricsScenario() {
    }

    public static void run() throws Exception {
        TriggerOperations.runMetricsB1(
            Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"),
            Path.of(Env.get("ZLINK_JAVA_E2E_SCENARIO_OUTPUT")));
    }
}
