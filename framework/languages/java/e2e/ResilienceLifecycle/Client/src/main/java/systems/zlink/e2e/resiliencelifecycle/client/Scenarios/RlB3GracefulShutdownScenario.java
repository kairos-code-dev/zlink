package systems.zlink.e2e.resiliencelifecycle.client.Scenarios;

import systems.zlink.e2e.resiliencelifecycle.client.Support.ResilienceScenarioContext;
import java.time.Duration;
import java.util.Set;
import systems.zlink.e2e.resiliencelifecycle.shared.Contracts;

public final class RlB3GracefulShutdownScenario {
    private RlB3GracefulShutdownScenario() {
    }

    public static void run(ResilienceScenarioContext context) {
        context.waitForTopology(2);
        Contracts.WorkRes before = context.request("b3-before-shutdown", Duration.ofSeconds(3));
        ResilienceScenarioContext.ensure("work:b3-before-shutdown".equals(before.value()),
            "RL-B3 pre-shutdown reply payload mismatch");
        context.post(context.adminB() + "/admin/shutdown");
        context.waitForTopologyWithout("api-b", 30);
        Set<String> providers = context.collectStableProvidersWithoutFailures(
            "b3-after-shutdown", "api-b", "api-a");
        ResilienceScenarioContext.ensure(providers.contains("api-a"),
            "RL-B3 did not converge to api-a after api-b shutdown");
        System.out.println("scenario RL-B3 passed");
    }
}
