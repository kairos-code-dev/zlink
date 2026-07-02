package systems.zlink.e2e.resiliencelifecycle.client;

import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlA1ProviderRestartScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlA2ProviderEndpointRemapScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlA3ReconnectStormScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlA5ProviderFlappingScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlB1CancellationCleanupScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlB3GracefulShutdownScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlB4RuntimeDrainScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlB5DrainInflightScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlB6GrayFaultScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlC1ClientHostLifecycleScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlD3DispatchErrorEvidenceScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Scenarios.RlD5MixedBurstScenario;
import systems.zlink.e2e.resiliencelifecycle.client.Support.ClientOptions;
import systems.zlink.e2e.resiliencelifecycle.client.Support.ConsumerScenarioClient;
import systems.zlink.e2e.resiliencelifecycle.client.Support.ResilienceProcessManager;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;

public final class Program {
    private Program() {
    }

    public static void main(String... args) {
        String mode = Env.get("ZLINK_JAVA_E2E_CLIENT_MODE", "default");
        if ("suite".equals(mode)) {
            ClientOptions options = ClientOptions.fromEnv();
            try (ResilienceProcessManager processes = new ResilienceProcessManager(options)) {
                new ResilienceLifecycleSuite(options, processes)
                    .run(Env.get("ZLINK_JAVA_E2E_SCENARIO", "all"));
            }
            System.out.println("resilience-lifecycle e2e result=passed");
            return;
        }
        String endpoint = Env.get("ZLINK_JAVA_E2E_CONSUMER_HTTP_ENDPOINT");
        ConsumerScenarioClient consumer = new ConsumerScenarioClient(endpoint);
        switch (mode) {
            case "restart" -> RlA1ProviderRestartScenario.run(consumer);
            case "reschedule" -> RlA2ProviderEndpointRemapScenario.run(consumer);
            case "flapping" -> RlA5ProviderFlappingScenario.run(consumer);
            case "storm" -> RlA3ReconnectStormScenario.run(consumer);
            case "cleanup" -> {
                RlC1ClientHostLifecycleScenario.run(consumer);
                RlD5MixedBurstScenario.run(consumer);
            }
            case "default" -> {
                RlB1CancellationCleanupScenario.run(consumer);
                RlB4RuntimeDrainScenario.run(consumer);
                RlB5DrainInflightScenario.run(consumer);
                RlD3DispatchErrorEvidenceScenario.run(consumer);
                RlB6GrayFaultScenario.run(consumer);
                RlB3GracefulShutdownScenario.run(consumer);
            }
            default -> throw new IllegalArgumentException("unknown ResilienceLifecycle client mode: " + mode);
        }
        System.out.println("resilience-lifecycle e2e result=passed");
    }
}
