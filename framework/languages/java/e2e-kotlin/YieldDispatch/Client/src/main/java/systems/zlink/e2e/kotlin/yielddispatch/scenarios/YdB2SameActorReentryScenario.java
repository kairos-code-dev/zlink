package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB2SameActorReentryScenario {
    private YdB2SameActorReentryScenario() {
    }

    public static void run(ZLinkStreamConnector connector, String actorId) {
        CompletableFuture<Contracts.ProbeReply> slow = CompletableFuture.supplyAsync(() ->
            ClientStreamSupport.request(connector, actorId, "same-actor-slow", 650));
        ClientStreamSupport.sleep(120);
        long started = System.nanoTime();
        Contracts.ProbeReply fast = ClientStreamSupport.request(connector, actorId, "same-actor-fast", 0);
        long elapsedMillis = Duration.ofNanos(System.nanoTime() - started).toMillis();
        Contracts.ProbeReply first = slow.join();
        ScenarioAssert.that(first.value().startsWith("delay:same-actor-slow#"), "YD-B2 slow reply mismatch");
        ScenarioAssert.that(fast.value().startsWith("immediate:same-actor-fast#"), "YD-B2 fast reply order mismatch");
        ScenarioAssert.that(elapsedMillis >= 400, "YD-B2 same actor request reentered before continuation");
        System.out.println("scenario YD-B2 passed");
    }
}
