package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonC1DispatchFailureScenario {
    private MonC1DispatchFailureScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        context.waitForEvent(context.serviceEndpoint(), "monitoring", Set.of("HandlerFailureInjected"));
        var reply = context.request("c1-after-handler-failure");
        MonitoringScenarioContext.ensure(reply.value().equals("work:c1-after-handler-failure"),
            "MON-C1 follow-up reply mismatch");
        System.out.println("scenario MON-C1 passed");
    }
}
