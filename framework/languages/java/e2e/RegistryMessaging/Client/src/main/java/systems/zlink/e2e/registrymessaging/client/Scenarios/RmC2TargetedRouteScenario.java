package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmC2TargetedRouteScenario {
    private RmC2TargetedRouteScenario() {
    }

    public static void run(ZLinkHttpClient providerA) {
        Contracts.RoutePong toB = providerA.post("/profile/route/request")
            .body(new Contracts.RoutePing("target-b"))
            .fetch(Contracts.RoutePong.class);
        ScenarioAssert.that("api-b".equals(toB.targetRid()), "RM-C2 target rid mismatch");

        Contracts.RouteMissingResult missing = providerA.post("/profile/route/missing")
            .body(new Contracts.RoutePing("missing"))
            .fetch(Contracts.RouteMissingResult.class);
        ScenarioAssert.that(missing.failed(), "RM-C2 missing rid request should fail");
        System.out.println("scenario RM-C2 passed");
    }
}
