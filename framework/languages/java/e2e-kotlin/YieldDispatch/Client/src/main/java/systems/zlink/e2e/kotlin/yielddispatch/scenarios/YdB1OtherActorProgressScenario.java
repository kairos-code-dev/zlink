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
        Contracts.BindActorsRes bind = ClientStreamSupport.bindActors(
            roomA,
            "room-a",
            "actor-room-a",
            "actor-room-b");
        ScenarioAssert.that("actor-room-a".equals(bind.actorA()), "YD-B1 bind actor A mismatch");
        ScenarioAssert.that("actor-room-b".equals(bind.actorB()), "YD-B1 bind actor B mismatch");
        ClientStreamSupport.bindActors(
            roomB,
            "room-a",
            "actor-room-a",
            "actor-room-b");
        Contracts.ActorJoinRes joinedA = ClientStreamSupport.joinActor(
            roomA,
            "actor-room-a",
            "room-a",
            "initial-a");
        ScenarioAssert.that("room-a".equals(joinedA.spotRid()), "YD-B1 join spot mismatch");
        ScenarioAssert.that("joined:initial-a".equals(joinedA.value()), "YD-B1 join yield reply mismatch");
        Contracts.ActorJoinRes joinedB = ClientStreamSupport.joinActor(
            roomB,
            "actor-room-b",
            "room-b",
            "initial-b");
        ScenarioAssert.that("room-b".equals(joinedB.spotRid()), "YD-B1 join actor B spot mismatch");
        ScenarioAssert.that("joined:initial-b".equals(joinedB.value()), "YD-B1 join actor B reply mismatch");
        String requestId = "YD-B1-" + UUID.randomUUID().toString().replace("-", "");
        CompletionStage<Contracts.ActorRes> yield = roomA
            .request(new Contracts.ActorYieldReq(requestId, 1200))
            .metadata("actor-id", "actor-room-a")
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        ClientStreamSupport.sleep(250);
        CompletionStage<Contracts.ActorRes> fast = roomB
            .request(new Contracts.ActorFastReq(requestId, "b1-fast"))
            .metadata("actor-id", "actor-room-b")
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        Contracts.ActorRes fastReply = fast.toCompletableFuture().join();
        Contracts.ActorRes yieldReply = yield.toCompletableFuture().join();
        ScenarioAssert.that("actor-room-a".equals(yieldReply.actorId()), "YD-B1 yield actor mismatch");
        ScenarioAssert.that("actor-room-b".equals(fastReply.actorId()), "YD-B1 fast actor mismatch");
        Contracts.EvidenceRes evidence = ClientStreamSupport.evidence(roomA, requestId);
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
