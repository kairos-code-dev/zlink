package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlD4MissingRequestHandlerScenario {
    private RlD4MissingRequestHandlerScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("rl-d4");
    }
}
