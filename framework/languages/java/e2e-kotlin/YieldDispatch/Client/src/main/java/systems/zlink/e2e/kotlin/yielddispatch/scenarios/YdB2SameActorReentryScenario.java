package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import java.util.concurrent.CompletionStage;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB2SameActorReentryScenario {
    private YdB2SameActorReentryScenario() {
    }

    public static void run(ZLinkStreamConnector connector, String actorId) {
        String requestId = "YD-B2-" + UUID.randomUUID().toString().replace("-", "");
        CompletionStage<Contracts.ActorRes> yield = connector
            .request(new Contracts.ActorYieldReq(requestId, 350))
            .metadata("actor-id", actorId)
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        ClientStreamSupport.sleep(75);
        CompletionStage<Contracts.ActorRes> fast = connector
            .request(new Contracts.ActorFastReq(requestId, "b2-fast"))
            .metadata("actor-id", actorId)
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        Contracts.ActorRes yieldReply = yield.toCompletableFuture().join();
        Contracts.ActorRes fastReply = fast.toCompletableFuture().join();
        ScenarioAssert.that(actorId.equals(yieldReply.actorId()), "YD-B2 yield actor mismatch");
        ScenarioAssert.that(actorId.equals(fastReply.actorId()), "YD-B2 fast actor mismatch");
        Contracts.EvidenceRes evidence = ClientStreamSupport.evidence(connector, requestId);
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed",
            "actor-fast-started",
            "actor-fast-completed");
        System.out.println("scenario YD-B2 passed");
    }
}
