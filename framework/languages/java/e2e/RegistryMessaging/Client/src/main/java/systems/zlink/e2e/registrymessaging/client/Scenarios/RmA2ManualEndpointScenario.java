package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmA2ManualEndpointScenario {
    private RmA2ManualEndpointScenario() {
    }

    public static void run(ZLinkHttpClient providerA) {
        Contracts.ProfileRes manual = providerA.post("/profile/manual")
            .body(new Contracts.ProfileReq("manual"))
            .async(Contracts.ProfileRes.class).toCompletableFuture().join().body();
        ScenarioAssert.that("api-a".equals(manual.providerRid()), "RM-A2 wrong provider");
        System.out.println("scenario RM-A2 passed");
    }
}
