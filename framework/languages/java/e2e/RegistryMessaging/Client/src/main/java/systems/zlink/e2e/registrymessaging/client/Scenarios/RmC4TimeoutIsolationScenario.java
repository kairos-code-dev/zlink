package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioSignals;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmC4TimeoutIsolationScenario {
    private RmC4TimeoutIsolationScenario() {
    }

    public static void run(ZLinkHttpClient discoveryConsumer) {
        Contracts.RequestFailureResult timeout = discoveryConsumer.post("/profile/slow-request")
            .body(new Contracts.ProfileRequest("slow"))
            .fetch(Contracts.RequestFailureResult.class);
        ScenarioAssert.that(timeout.failed(), "RM-C4 expected slow request failure");
        Contracts.ProfileReply after = discoveryConsumer.post("/profile/request")
            .body(new Contracts.ProfileRequest("after"))
            .fetch(Contracts.ProfileReply.class);
        ScenarioAssert.that("profile:after".equals(after.value()), "RM-C4 post-timeout request failed");
        ScenarioSignals.sleep(1100);
        Contracts.ProfileReply later = discoveryConsumer.post("/profile/request")
            .body(new Contracts.ProfileRequest("later"))
            .fetch(Contracts.ProfileReply.class);
        ScenarioAssert.that("profile:later".equals(later.value()), "RM-C4 later request failed");
        System.out.println("scenario RM-C4 passed");
    }
}
