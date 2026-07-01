package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlB2CrashDuringInflightScenario {
    private RlB2CrashDuringInflightScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        ConsumerScenarioClient.unsupported("RL-B2", "provider crash during in-flight request still has a Java shutdown gap");
    }
}
