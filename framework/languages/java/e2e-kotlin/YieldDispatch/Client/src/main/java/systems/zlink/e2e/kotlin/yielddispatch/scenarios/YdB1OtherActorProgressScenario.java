package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import java.util.concurrent.CompletionStage;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdB1OtherActorProgressScenario {
    private YdB1OtherActorProgressScenario() {
    }

    public static JoinedActors run(ZLinkStreamConnector roomA, ZLinkStreamConnector roomB) {
        Contracts.ActorJoinReply joinedA = ClientStreamSupport.joinActor(
            roomA,
            "actor-room-a",
            "room-a",
            "initial-a");
        ScenarioAssert.that("room-a".equals(joinedA.spotRid()), "YD-B1 join spot mismatch");
        ScenarioAssert.that("joined:initial-a".equals(joinedA.value()), "YD-B1 join yield reply mismatch");
        Contracts.ActorJoinReply joinedB = ClientStreamSupport.joinActor(
            roomB,
            "actor-room-b",
            "room-b",
            "initial-b");
        ScenarioAssert.that("room-b".equals(joinedB.spotRid()), "YD-B1 join actor B spot mismatch");
        ScenarioAssert.that("joined:initial-b".equals(joinedB.value()), "YD-B1 join actor B reply mismatch");
        String requestId = "YD-B1-" + UUID.randomUUID().toString().replace("-", "");
        CompletionStage<Contracts.ActorReply> yield = roomA
            .request(new Contracts.ActorYieldRequest(requestId, 1200))
            .metadata("actor-id", "actor-room-a")
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);
        ClientStreamSupport.sleep(250);
        CompletionStage<Contracts.ActorReply> fast = roomB
            .request(new Contracts.ActorFastRequest(requestId, "b1-fast"))
            .metadata("actor-id", "actor-room-b")
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);
        Contracts.ActorReply fastReply = fast.toCompletableFuture().join();
        Contracts.ActorReply yieldReply = yield.toCompletableFuture().join();
        ScenarioAssert.that("actor-room-a".equals(yieldReply.actorId()), "YD-B1 yield actor mismatch");
        ScenarioAssert.that("actor-room-b".equals(fastReply.actorId()), "YD-B1 fast actor mismatch");
        Contracts.EvidenceReply evidence = ClientStreamSupport.evidence(roomA, requestId);
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "actor-yield-started",
            "actor-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed");
        System.out.println("scenario YD-B1 passed");
        return new JoinedActors("actor-room-a", "actor-room-b");
    }

    public record JoinedActors(String actorA, String actorB) {
    }
}
