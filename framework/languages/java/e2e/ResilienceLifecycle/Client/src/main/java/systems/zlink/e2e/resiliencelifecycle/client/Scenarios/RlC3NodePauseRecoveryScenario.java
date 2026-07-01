package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlC3NodePauseRecoveryScenario {
    private RlC3NodePauseRecoveryScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("restart");
    }
}
