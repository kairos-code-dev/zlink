package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;

public final class RlA2ProviderEndpointRemapScenario {
    private RlA2ProviderEndpointRemapScenario() {
    }

    public static void run(ConsumerScenarioClient consumer) {
        consumer.runMode("reschedule");
    }
}
