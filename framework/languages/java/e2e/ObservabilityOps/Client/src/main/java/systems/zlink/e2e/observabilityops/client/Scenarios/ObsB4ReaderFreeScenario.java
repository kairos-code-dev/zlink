package systems.zlink.e2e.observabilityops.client.Scenarios;

import java.nio.file.Path;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.e2e.observabilityops.trigger.TriggerOperations;

public final class ObsB4ReaderFreeScenario {
    private ObsB4ReaderFreeScenario() {
    }

    public static void run() throws Exception {
        TriggerOperations.runReaderFreeB4(
            Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"),
            Path.of(Env.get("ZLINK_JAVA_E2E_SCENARIO_OUTPUT")));
    }
}
