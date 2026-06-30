package systems.zlink.e2e.registrationcodec.client.Scenarios;

import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrationcodec.client.Support.ScenarioContext;
import systems.zlink.e2e.registrationcodec.shared.Contracts;

public final class RcA4DiLifecycleScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);

    private RcA4DiLifecycleScenario() {
    }

    public static void run(ScenarioContext context) {
        List<Contracts.DiLifecycleRes> replies = new ArrayList<>();
        for (int index = 0; index < 3; index++) {
            replies.add(context.client().requestToChannel(
                    Contracts.CHANNEL,
                    new Contracts.DiLifecycleReq("di-" + index))
                .packetName("DiLifecycle")
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.DiLifecycleRes.class));
        }
        ScenarioAssert.ensure(replies.stream().map(Contracts.DiLifecycleRes::scopedId).distinct().count() == 3,
            "RC-A4 scoped dependency was not recreated per request: " + replies);
        ScenarioAssert.ensure(replies.stream().map(Contracts.DiLifecycleRes::singletonId).distinct().count() == 1,
            "RC-A4 singleton dependency changed between requests: " + replies);
        ScenarioAssert.ensure(replies.get(2).disposedCount() == 3,
            "RC-A4 dispose count mismatch: " + replies);
        ScenarioAssert.waitForEvidenceValueSuffix(context.evidence(), "DI", "DiLifecycle", ":di-2");
        System.out.println("scenario RC-A4 passed");
    }
}
