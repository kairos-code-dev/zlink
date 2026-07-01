package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.DynamicClusterLauncher;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmA4SameRidFailoverScenario {
    private RmA4SameRidFailoverScenario() {
    }

    public static void run() {
        try (DynamicClusterLauncher cluster = DynamicClusterLauncher.start("rm-a4")) {
            DynamicClusterLauncher.DynamicProvider providerV1 =
                cluster.startProvider("api-a-v1", "api-a", "api-a-v1", "");
            try (ZLinkHttpClient requester = ZLinkHttpClient.create(providerV1.httpUrl()).build()) {
                Contracts.ProfileRes first = requester.post("/profile/request")
                    .body(new Contracts.ProfileReq("failover-before"))
                    .fetch(Contracts.ProfileRes.class);
                ScenarioAssert.that("api-a".equals(first.providerRid()) && "api-a-v1".equals(first.instanceId()),
                    "RM-A4 initial provider mismatch");
            }

            cluster.stop(providerV1);
            DynamicClusterLauncher.DynamicProvider providerV2 =
                cluster.startProvider("api-a-v2", "api-a", "api-a-v2", "");
            try (ZLinkHttpClient requester = ZLinkHttpClient.create(providerV2.httpUrl()).build()) {
                for (int index = 0; index < 20; index++) {
                    Contracts.ProfileRes reply = requester.post("/profile/request")
                        .body(new Contracts.ProfileReq("failover-after-" + index))
                        .fetch(Contracts.ProfileRes.class);
                    ScenarioAssert.that("api-a".equals(reply.providerRid()) && "api-a-v2".equals(reply.instanceId()),
                        "RM-A4 did not switch to replacement provider");
                }
            }
        }
        System.out.println("scenario RM-A4 passed");
    }
}
