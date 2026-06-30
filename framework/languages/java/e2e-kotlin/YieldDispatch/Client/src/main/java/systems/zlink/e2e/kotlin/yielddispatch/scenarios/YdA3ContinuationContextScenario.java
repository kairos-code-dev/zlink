package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdA3ContinuationContextScenario {
    private YdA3ContinuationContextScenario() {
    }

    public static void run(
        ZLinkStreamConnector roomA,
        String actorA,
        ZLinkStreamConnector roomB,
        String actorB) {
        CompletableFuture<Contracts.ProbeRes> slow = CompletableFuture.supplyAsync(() ->
            ClientStreamSupport.request(roomA, actorA, "room-a", "cross-slow", 900));
        ClientStreamSupport.sleep(120);
        long started = System.nanoTime();
        Contracts.ProbeRes other = ClientStreamSupport.request(roomB, actorB, "room-b", "cross-other", 0);
        long elapsedMillis = Duration.ofNanos(System.nanoTime() - started).toMillis();
        slow.join();
        ScenarioAssert.that(
            other.value().startsWith("immediate:cross-other#1"),
            "YD-A3 cross spot reply mismatch: " + other.value());
        ScenarioAssert.that(elapsedMillis < 650, "YD-A3 independent spot was blocked by another spot yield");
        System.out.println("scenario YD-A3 passed");
    }
}
