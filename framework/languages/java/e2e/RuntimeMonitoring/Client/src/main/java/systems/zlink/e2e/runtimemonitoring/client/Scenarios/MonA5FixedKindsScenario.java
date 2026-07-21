package systems.zlink.e2e.runtimemonitoring.client.Scenarios;

import java.time.Instant;
import systems.zlink.e2e.runtimemonitoring.client.Support.MonitoringScenarioContext;
import systems.zlink.e2e.runtimemonitoring.shared.Contracts;

public final class MonA5FixedKindsScenario {
    private static final String STORE_CHANGED =
        "zlink.runtime.location.store_changed";

    private MonA5FixedKindsScenario() {
    }

    public static void run(MonitoringScenarioContext context) {
        Contracts.RuntimeSnapshot baseline = context.awaitRuntimeSnapshot(
            context.serviceEndpoint(),
            snapshot -> "ready".equals(snapshot.locationState()),
            "MON-A5 location runtime was not ready");
        MonitoringScenarioContext.ensure(
            !baseline.locationLastSuccess().isBlank()
                && baseline.locationLastFailure().isBlank(),
            "MON-A5 normal location health timestamps were incomplete");
        int evidenceStart = context.evidenceEntryCount(context.serviceEndpoint());

        context.setRedisPaused(true);
        try {
            Contracts.RuntimeSnapshot degraded = context.awaitRuntimeSnapshot(
                context.serviceEndpoint(),
                snapshot -> "degraded".equals(snapshot.locationState()),
                "MON-A5 location runtime did not become degraded");
            MonitoringScenarioContext.ensure(
                !degraded.locationLastFailure().isBlank()
                    && !Instant.parse(degraded.locationLastFailure()).isBefore(
                        Instant.parse(baseline.locationLastSuccess())),
                "MON-A5 degraded snapshot did not record the store failure");
            MonitoringScenarioContext.ensure(
                degraded.peers().stream().anyMatch(Contracts.RuntimePeer::ready)
                    && degraded.channels().stream().anyMatch(channel ->
                        Contracts.SPOT_CHANNEL.equals(channel.channelName())
                            && channel.selectable()),
                "MON-A5 store outage removed the admitted messaging path: " + degraded);
            Contracts.WorkRes reply =
                context.runtimeRequest(context.serviceEndpoint(), "mon-a5-during-outage");
            MonitoringScenarioContext.ensure(
                "work:mon-a5-during-outage".equals(reply.value()),
                "MON-A5 admitted messaging failed during the store outage");
            context.waitForRouteMeshEventReason(
                context.serviceEndpoint(),
                evidenceStart,
                STORE_CHANGED,
                "degraded");
        } finally {
            context.setRedisPaused(false);
        }

        Contracts.RuntimeSnapshot recovered = context.awaitRuntimeSnapshot(
            context.serviceEndpoint(),
            snapshot -> "ready".equals(snapshot.locationState())
                && !snapshot.locationLastFailure().isBlank()
                && Instant.parse(snapshot.locationLastSuccess()).isAfter(
                    Instant.parse(snapshot.locationLastFailure()))
                && snapshot.peers().stream().anyMatch(Contracts.RuntimePeer::ready),
            "MON-A5 recovered snapshot did not revalidate the current topology");
        MonitoringScenarioContext.ensure(
            "ready".equals(recovered.locationState()),
            "MON-A5 recovered location state mismatch");
        context.waitForRouteMeshEventReason(
            context.serviceEndpoint(),
            evidenceStart,
            STORE_CHANGED,
            "ready");
        System.out.println("scenario MON-A5 passed");
    }
}
