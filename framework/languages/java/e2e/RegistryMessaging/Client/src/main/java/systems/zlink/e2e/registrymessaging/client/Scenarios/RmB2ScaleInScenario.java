package systems.zlink.e2e.registrymessaging.client.Scenarios;

import java.util.HashSet;
import java.util.Set;
import systems.zlink.e2e.registrymessaging.client.Support.ClientOptions;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioSignals;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmB2ScaleInScenario {
    private RmB2ScaleInScenario() {
    }

    public static void run(ZLinkHttpClient discoveryConsumer) {
        Set<String> before = new HashSet<>();
        for (int index = 0; index < 100 && before.size() < 2; index++) {
            Contracts.ProfileRes reply = discoveryConsumer.post("/profile/request")
                .body(new Contracts.ProfileReq("scale-in-before-" + index))
                .fetch(Contracts.ProfileRes.class);
            before.add(reply.providerRid());
        }
        ScenarioAssert.that(before.contains("api-a") && before.contains("api-b"),
            "RM-B2 did not start with both providers");

        ScenarioSignals.touch(ClientOptions.get("ZLINK_JAVA_E2E_READY_FILE"));
        ScenarioSignals.waitForFile(ClientOptions.get("ZLINK_JAVA_E2E_CONTINUE_FILE"));

        for (int index = 0; index < 20; index++) {
            Contracts.ProfileRes reply = discoveryConsumer.post("/profile/request")
                .body(new Contracts.ProfileReq("scale-in-after-" + index))
                .fetch(Contracts.ProfileRes.class);
            ScenarioAssert.that("api-a".equals(reply.providerRid()), "RM-B2 routed to removed provider");
        }
        System.out.println("scenario RM-B2 passed");
    }
}
