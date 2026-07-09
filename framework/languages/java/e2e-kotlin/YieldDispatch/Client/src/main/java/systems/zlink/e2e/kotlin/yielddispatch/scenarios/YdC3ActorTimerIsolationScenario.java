package systems.zlink.e2e.kotlin.yielddispatch.scenarios;

import java.util.List;
import java.util.UUID;
import java.util.concurrent.CompletionStage;
import systems.zlink.e2e.kotlin.yielddispatch.Contracts;
import systems.zlink.e2e.kotlin.yielddispatch.support.ClientStreamSupport;
import systems.zlink.e2e.kotlin.yielddispatch.support.ScenarioAssert;
import systems.zlink.stream.connector.ZLinkStreamConnector;

public final class YdC3ActorTimerIsolationScenario {
    private YdC3ActorTimerIsolationScenario() {
    }

    public static void run(ZLinkStreamConnector connector) {
        String suffix = UUID.randomUUID().toString().replace("-", "");
        String spotRid = "yield-c3-" + suffix;
        String actorA = "actor-c3-a-" + suffix;
        String actorB = "actor-c3-b-" + suffix;
        ClientStreamSupport.bindActors(connector, spotRid, actorA, actorB);
        ClientStreamSupport.joinActor(connector, actorA, spotRid, "c3-a");
        ClientStreamSupport.joinActor(connector, actorB, spotRid, "c3-b");
        verifyActorYieldAllowsTimer(connector, spotRid, actorA);
        verifyTimerYieldAllowsActor(connector, spotRid, actorB);
        System.out.println("scenario YD-C3 passed");
    }

    private static void verifyActorYieldAllowsTimer(
        ZLinkStreamConnector connector,
        String spotRid,
        String actorId) {
        String requestId = "YD-C3-actor-" + UUID.randomUUID().toString().replace("-", "");
        CompletionStage<Contracts.ActorRes> yield = connector
            .request(new Contracts.ActorYieldReq(requestId, 1200))
            .metadata("actor-id", actorId)
            .metadata(Contracts.SPOT_RID_METADATA, spotRid)
            .timeout(ClientStreamSupport.REQUEST_TIMEOUT)
            .submit(Contracts.ActorRes.class);
        ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "actor-yield-released");
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    requestId + "-fast",
                    "fast",
                    50,
                    0))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "timer-fast-completed");
        yield.toCompletableFuture().join();
        Contracts.EvidenceRes evidence =
            ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "actor-yield-completed");
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed");
    }

    private static void verifyTimerYieldAllowsActor(
        ZLinkStreamConnector connector,
        String spotRid,
        String actorId) {
        String requestId = "YD-C3-timer-" + UUID.randomUUID().toString().replace("-", "");
        ClientStreamSupport.send(
            connector.send(new Contracts.TimerStartMsg(
                    requestId,
                    requestId + "-yield",
                    "yield-on-first",
                    50,
                    1200))
                .metadata(Contracts.TARGET_NODE_RID_METADATA, "play-a")
                .metadata(Contracts.SPOT_RID_METADATA, spotRid));
        ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "timer-yield-released");
        Contracts.ActorRes fast = ClientStreamSupport.await(
            connector.request(new Contracts.ActorFastReq(requestId, "c3-actor-fast"))
                .metadata("actor-id", actorId)
                .metadata(Contracts.SPOT_RID_METADATA, spotRid)
                .timeout(ClientStreamSupport.REQUEST_TIMEOUT),
            Contracts.ActorRes.class);
        ScenarioAssert.that(actorId.equals(fast.actorId()), "YD-C3 fast actor mismatch");
        Contracts.EvidenceRes evidence =
            ClientStreamSupport.waitForEvidence(connector, requestId, spotRid, "timer-yield-completed");
        ScenarioAssert.containsMarkersInOrder(evidence.markers(),
            "timer-yield-started",
            "timer-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed");
        ScenarioAssert.that(
            evidence.markers().stream().anyMatch(entry -> entry.contains("marker=c3-actor-fast")),
            "YD-C3 actor fast marker missing: " + List.copyOf(evidence.markers()));
    }
}
