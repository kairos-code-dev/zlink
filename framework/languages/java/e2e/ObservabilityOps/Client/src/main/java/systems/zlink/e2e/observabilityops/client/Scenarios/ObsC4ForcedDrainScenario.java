package systems.zlink.e2e.observabilityops.client.Scenarios;

import java.nio.file.Path;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.observabilityops.trigger.TriggerOperations;

public final class ObsC4ForcedDrainScenario {
    private ObsC4ForcedDrainScenario() {
    }

    public static void run() throws Exception {
        TriggerOperations.runDrainWatch(
            Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"),
            Env.get("ZLINK_JAVA_E2E_DRAIN_URL"),
            Path.of(Env.get("ZLINK_JAVA_E2E_SCENARIO_OUTPUT")));
    }
}
