package systems.zlink.e2e.registrymessaging.client.Scenarios;

import java.util.HashSet;
import java.util.Set;
import systems.zlink.e2e.registrymessaging.client.Support.DynamicClusterLauncher;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmB1ScaleOutScenario {
    private RmB1ScaleOutScenario() {
    }

    public static void run() {
        try (DynamicClusterLauncher cluster = DynamicClusterLauncher.start("rm-b1")) {
            DynamicClusterLauncher.DynamicProvider providerA = cluster.startProvider("api-a", "api-a");
            try (ZLinkHttpClient requester = ZLinkHttpClient.create(providerA.httpUrl()).build()) {
                for (int index = 0; index < 5; index++) {
                    Contracts.ProfileRes reply = requester.post("/profile/request")
                        .body(new Contracts.ProfileReq("scale-out-before-" + index))
                        .fetch(Contracts.ProfileRes.class);
                    ScenarioAssert.that("api-a".equals(reply.providerRid()),
                        "RM-B1 initial traffic should only use api-a");
                }

                cluster.startProvider("api-b", "api-b");

                Set<String> providers = new HashSet<>();
                for (int index = 0; index < 100 && providers.size() < 2; index++) {
                    Contracts.ProfileRes reply = requester.post("/profile/request")
                        .body(new Contracts.ProfileReq("scale-out-after-" + index))
                        .fetch(Contracts.ProfileRes.class);
                    providers.add(reply.providerRid());
                }
                ScenarioAssert.that(providers.contains("api-a") && providers.contains("api-b"),
                    "RM-B1 did not route to both providers after scale-out");
            }
        }
        System.out.println("scenario RM-B1 passed");
    }
}
