package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonD1FailureRecoveryScenario {
    private MonD1FailureRecoveryScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String serviceA = context.serviceEndpoint();
        String serviceB = context.serviceBEndpoint();
        MonitoringScenarioContext.ensure(!serviceB.isBlank(), "MON-D1 requires service-b HTTP endpoint");
        context.shutdownServiceB("MON-D1 expected service-b to stop");

        context.restartServiceB();
        try {
            context.waitForPort(serviceB, true, "MON-D1 expected service-b to restart");
            context.post(serviceA, "/admin/drain");
            var reply = context.requestFromProvider("mon-d1-request", "svc-b");
            MonitoringScenarioContext.ensure(
                reply.providerRid().equals("svc-b") && reply.value().equals("work:mon-d1-request"),
                "MON-D1 restarted service did not handle request");
            context.waitForEvent(serviceB, "work", Set.of("WorkReq"));
            context.waitForLocationEventCount(serviceA, 3);
            System.out.println("scenario MON-D1 passed");
        } finally {
            context.postBestEffort(serviceA, "/admin/restore");
            context.stopRestartedServiceB();
        }
    }
}
