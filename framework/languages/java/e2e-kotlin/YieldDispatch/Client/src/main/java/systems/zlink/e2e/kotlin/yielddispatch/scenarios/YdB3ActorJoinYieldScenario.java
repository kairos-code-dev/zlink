package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB3ActorJoinYieldScenario {
    private YdB3ActorJoinYieldScenario() {
    }

    public static void run(
        ZLinkStreamConnector joinConnector,
        String joiningActorId,
        ZLinkStreamConnector fastConnector,
        String fastActorId) {
        CompletableFuture<Contracts.ActorJoinRes> join = CompletableFuture.supplyAsync(() ->
            ClientStreamSupport.joinActor(joinConnector, joiningActorId, "room-b", "join-yield", 650));
        ClientStreamSupport.sleep(120);
        long started = System.nanoTime();
        Contracts.ProbeRes fast = ClientStreamSupport.request(fastConnector, fastActorId, "join-fast", 0);
        long elapsedMillis = Duration.ofNanos(System.nanoTime() - started).toMillis();
        Contracts.ActorJoinRes joined = join.join();
        ScenarioAssert.that("room-b".equals(joined.spotRid()), "YD-B3 join spot mismatch");
        ScenarioAssert.that("joined:join-yield".equals(joined.value()), "YD-B3 join reply mismatch");
        ScenarioAssert.that(fast.value().startsWith("immediate:join-fast#"), "YD-B3 fast reply mismatch");
        ScenarioAssert.that(elapsedMillis < 400, "YD-B3 other actor did not progress while join yielded");
        System.out.println("scenario YD-B3 passed");
    }
}
