package systems.zlink.e2e.registrymessaging.client.Scenarios;

import systems.zlink.e2e.registrymessaging.client.Support.ScenarioAssert;
import systems.zlink.e2e.registrymessaging.client.Support.ScenarioSignals;
import systems.zlink.e2e.registrymessaging.shared.Contracts;
import systems.zlink.httpclient.ZLinkHttpClient;

public final class RmC9BackpressureScenario {
    private RmC9BackpressureScenario() {
    }

    public static void run(ZLinkHttpClient backpressureConsumer, ZLinkHttpClient providerA) {
        backpressureConsumer.post("/profile/backpressure/reset").fetch(Object.class);
        java.util.List<java.util.concurrent.CompletableFuture<String>> sends = new java.util.ArrayList<>();
        for (int index = 0; index < 32; index++) {
            String commandId = "slow-c9-" + index;
            sends.add(java.util.concurrent.CompletableFuture.supplyAsync(() ->
                backpressureConsumer.post("/profile/backpressure/send")
                    .body(new Contracts.ProfileMsg(commandId))
                    .fetch(Contracts.BackpressureRes.class)
                    .outcome()));
        }
        java.util.List<String> outcomes = sends.stream()
            .map(java.util.concurrent.CompletableFuture::join)
            .toList();
        ScenarioAssert.that(outcomes.stream().anyMatch("BoundedFailure"::equals),
            "RM-C9 did not observe bounded backpressure/timeout");
        ScenarioSignals.sleep(5000);

        Contracts.ProfileRes recovered = requestRecovered(backpressureConsumer);
        ScenarioAssert.that("profile:c9-recovered".equals(recovered.value()),
            "RM-C9 connection did not recover after pressure");
        String[] evidence = providerA.post("/evidence/wait")
            .body(new Contracts.EvidenceWaitReq("c9-recovered", 20000))
            .fetch(String[].class);
        ScenarioAssert.that(java.util.Arrays.stream(evidence).anyMatch(line -> line.contains("c9-recovered")),
            "RM-C9 recovery evidence missing");
        System.out.println("scenario RM-C9 passed");
    }

    private static Contracts.ProfileRes requestRecovered(ZLinkHttpClient backpressureConsumer) {
        long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(20);
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return backpressureConsumer.post("/profile/request")
                    .body(new Contracts.ProfileReq("c9-recovered"))
                    .fetch(Contracts.ProfileRes.class);
            } catch (RuntimeException error) {
                lastFailure = error;
                ScenarioSignals.sleep(500);
            }
        }
        throw lastFailure == null
            ? new IllegalStateException("RM-C9 recovery request did not run")
            : lastFailure;
    }
}
