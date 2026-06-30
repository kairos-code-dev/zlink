package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ClientOptions;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioSignals;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmA4SameRidFailoverScenario {
    private RmA4SameRidFailoverScenario() {
    }

    public static void run(ZLinkHttpClient discoveryConsumer) {
        Contracts.ProfileReply first = discoveryConsumer.post("/profile/request")
            .body(new Contracts.ProfileRequest("failover-before"))
            .fetch(Contracts.ProfileReply.class);
        ScenarioAssert.that("api-a".equals(first.providerRid()) && "api-a-v1".equals(first.instanceId()),
            "RM-A4 initial provider mismatch");

        ScenarioSignals.touch(ClientOptions.get("ZLINK_JAVA_E2E_READY_FILE"));
        ScenarioSignals.waitForFile(ClientOptions.get("ZLINK_JAVA_E2E_CONTINUE_FILE"));

        for (int index = 0; index < 20; index++) {
            Contracts.ProfileReply reply = discoveryConsumer.post("/profile/request")
                .body(new Contracts.ProfileRequest("failover-after-" + index))
                .fetch(Contracts.ProfileReply.class);
            ScenarioAssert.that("api-a".equals(reply.providerRid()) && "api-a-v2".equals(reply.instanceId()),
                "RM-A4 did not switch to replacement provider");
        }
        System.out.println("scenario RM-A4 passed");
    }
}
