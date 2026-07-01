package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioWait;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmC4TimeoutIsolationScenario {
    private RmC4TimeoutIsolationScenario() {
    }

    public static void run(ZLinkHttpClient discoveryConsumer) {
        Contracts.RequestFailureRes timeout = discoveryConsumer.post("/profile/slow-request")
            .body(new Contracts.ProfileReq("slow"))
            .fetch(Contracts.RequestFailureRes.class);
        ScenarioAssert.that(timeout.failed(), "RM-C4 expected slow request failure");
        Contracts.ProfileRes after = discoveryConsumer.post("/profile/request")
            .body(new Contracts.ProfileReq("after"))
            .fetch(Contracts.ProfileRes.class);
        ScenarioAssert.that("profile:after".equals(after.value()), "RM-C4 post-timeout request failed");
        ScenarioWait.sleep(1100);
        Contracts.ProfileRes later = discoveryConsumer.post("/profile/request")
            .body(new Contracts.ProfileReq("later"))
            .fetch(Contracts.ProfileRes.class);
        ScenarioAssert.that("profile:later".equals(later.value()), "RM-C4 later request failed");
        System.out.println("scenario RM-C4 passed");
    }
}
