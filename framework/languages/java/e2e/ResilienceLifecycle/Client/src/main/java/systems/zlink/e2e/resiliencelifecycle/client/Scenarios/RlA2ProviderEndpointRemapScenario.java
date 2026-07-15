package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ResilienceScenarioContext;
import java.time.Duration;
import systems.zlink.e2e.resiliencelifecycle.shared.Env;

public final class RlA2ProviderEndpointRemapScenario {
    private RlA2ProviderEndpointRemapScenario() {
    }

    public static void run(ResilienceScenarioContext context) {
        context.waitForTopology(2);
        context.post(context.adminB() + "/admin/drain");
        context.waitForWeight(context.adminB(), 0);
        context.collectStableProvidersWithout("a2-before-reschedule", "api-b", "api-a");
        context.signal("a2-ready");
        context.waitForSignal("a2-down");
        try {
            context.request("a2-down-window", Duration.ofMillis(700));
            throw new IllegalStateException("RL-A2 down-window request unexpectedly completed");
        } catch (RuntimeException expected) {
            // Public failure is required before the replacement endpoint is registered.
        }
        context.signal("a2-down-observed");
        context.waitForSignal("a2-up");
        context.waitForTopologyEndpoint(
            "api-a", Env.get("ZLINK_JAVA_E2E_API_A_REPLACEMENT_ENDPOINT"));
        context.collectStableProvidersWithout("a2-after-reschedule", "api-b", "api-a");
        context.post(context.adminB() + "/admin/restore");
        context.waitForWeight(context.adminB(), 100);
        System.out.println("scenario RL-A2 passed");
    }
}
