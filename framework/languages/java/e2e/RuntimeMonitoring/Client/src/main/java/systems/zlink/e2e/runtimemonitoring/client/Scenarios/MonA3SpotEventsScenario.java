package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.util.Set;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;

public final class MonA3SpotEventsScenario {
    private MonA3SpotEventsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        String service = context.serviceEndpoint();
        int subjectsBefore = context.latestEvidenceCount(
            service, "spot", "SUBJECTS_CHANGED", "subjects");
        int entriesBefore = context.evidenceEntryCount(service);
        context.post(service, "/admin/create-subject-spot");
        context.waitForEvidenceCountAfter(
            service, "spot", "SUBJECTS_CHANGED", "subjects", entriesBefore,
            count -> count > subjectsBefore,
            "MON-A3 spot subjects did not increase from " + subjectsBefore);
        context.waitForEvent(service, "spot", Set.of(
            "STATUS_CHANGED", "PEERS_CHANGED", "SUBJECTS_CHANGED", "TIMER_HANDLER_FAILED"));
        System.out.println("scenario MON-A3 passed");
    }
}
