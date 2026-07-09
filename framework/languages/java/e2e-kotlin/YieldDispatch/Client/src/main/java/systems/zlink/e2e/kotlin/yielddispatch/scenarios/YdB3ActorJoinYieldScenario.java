package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.UUID;
import java.util.concurrent.CompletionStage;
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
        String requestId = "YD-B3-" + UUID.randomUUID().toString().replace("-", "");
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsRes joinedSession = ClientStreamSupport.bindActors(
            joinConnector,
            "room-a",
            actorA,
            actorB);
        ScenarioAssert.that(actorA.equals(joinedSession.actorA()), "YD-B3 actor A bind mismatch");
        ScenarioAssert.that(actorB.equals(joinedSession.actorB()), "YD-B3 actor B bind mismatch");
        ClientStreamSupport.bindActors(
            fastConnector,
            "room-a",
            actorA,
            actorB);
        CompletionStage<Contracts.ActorRes> join = joinConnector
            .request(new Contracts.ActorJoinYieldReq(requestId, "room-a"))
            .metadata("actor-id", actorA)
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        ClientStreamSupport.sleep(75);
        CompletionStage<Contracts.ActorRes> fast = fastConnector
            .request(new Contracts.ActorFastReq(requestId, "b3-fast"))
            .metadata("actor-id", actorB)
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        Contracts.ActorRes fastReply = fast.toCompletableFuture().join();
        Contracts.ActorRes joinReply = join.toCompletableFuture().join();
        ScenarioAssert.that(actorA.equals(joinReply.actorId()), "YD-B3 join actor mismatch");
        ScenarioAssert.that(actorB.equals(fastReply.actorId()), "YD-B3 fast actor mismatch");
        Contracts.EvidenceRes evidence = ClientStreamSupport.evidence(joinConnector, requestId);
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "actor-join-yield-started",
            "actor-join-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-yield-resumed",
            "actor-join-yield-completed");
        System.out.println("scenario YD-B3 passed");
    }
}
