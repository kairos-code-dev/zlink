package systems.zlink.e2e.automaticturn.client.Support;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;
import systems.zlink.e2e.automaticturn.shared.Contracts;
import systems.zlink.e2e.automaticturn.shared.Env;
import systems.zlink.httpclient.RawHttpResponse;
import systems.zlink.httpclient.ZLinkHttpClient;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class AutomaticTurnDispatchScenarioSupport {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(30);
    private static final long ISOLATION_DELAY_MILLIS = 350;
    private static final long ACTOR_TIMER_ISOLATION_DELAY_MILLIS = 5000;
    private static final ObjectMapper JSON = new ObjectMapper();

    private AutomaticTurnDispatchScenarioSupport() {
    }

    public static void runBasicTerminator(ZLinkStreamConnector connector) throws Exception {
        runScenario(connector, "ATD-A1", List.of(
            "hold-started",
            "probe-started",
            "probe-completed",
            "hold-resumed",
            "hold-completed"));
    }

    public static void runAwaitTerminator(ZLinkStreamConnector connector) throws Exception {
        runScenario(connector, "ATD-A2", List.of(
            "await-started",
            "await-released",
            "probe-started",
            "probe-completed",
            "await-resumed",
            "await-completed"));
    }

    public static void runContinuationContext(ZLinkStreamConnector connector) throws Exception {
        runScenario(connector, "ATD-A3", List.of(
                "await-started",
                "await-released",
                "await-resumed",
                "await-completed"),
            Map.of(
                Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
                Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE),
            List.of(
                "spot=" + Contracts.TARGET_SPOT,
                "correlation=corr-a3"));
    }

    public static void runActorOtherProgress(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdb1-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsRes bind = connector
            .request(new Contracts.BindActorsReq(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.BindActorsRes.class).toCompletableFuture().join();
        ensure(actorA.equals(bind.actorA()), "ATD-B1 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "ATD-B1 actor B bind mismatch");

        joinActor(connector, requestId + "-join-a", actorA);
        joinActor(connector, requestId + "-join-b", actorB);

        CompletionStage<Contracts.ActorAwaitRes> await = connector
            .request(new Contracts.ActorAwaitReq(requestId, 800))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorAwaitRes.class);
        Thread.sleep(150);
        CompletionStage<Contracts.ActorFastRes> fast = connector
            .request(new Contracts.ActorFastReq(requestId, "b1-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorB)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorFastRes.class);

        Contracts.ActorFastRes fastReply = fast.toCompletableFuture().join();
        Contracts.ActorAwaitRes awaitReply = await.toCompletableFuture().join();
        ensure(actorA.equals(awaitReply.actorId()), "ATD-B1 await actor mismatch");
        ensure(actorB.equals(fastReply.actorId()), "ATD-B1 fast actor mismatch");
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        assertOrder(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-await-resumed",
            "actor-await-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "actor-await-resumed",
            "actor-await-completed"), "actor=" + actorA);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorB);
    }

    public static void runWorkerAwait(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atda4-" + System.nanoTime();
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        connector
            .send(new Contracts.WorkerAwaitMsg(requestId, 4000))
            .metadata(metadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "worker-await-started",
            "worker-await-released"));
        connector
            .send(new Contracts.ProbeMsg(requestId, "worker-probe"))
            .metadata(metadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "worker-await-started",
            "worker-await-released",
            "probe-started",
            "probe-completed",
            "worker-await-resumed",
            "worker-await-completed"));
    }

    public static void runSameActorReentry(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdb2-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsRes bind = connector
            .request(new Contracts.BindActorsReq(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.BindActorsRes.class).toCompletableFuture().join();
        ensure(actorA.equals(bind.actorA()), "ATD-B2 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "ATD-B2 actor B bind mismatch");

        joinActor(connector, requestId + "-join-a", actorA);

        CompletionStage<Contracts.ActorAwaitRes> await = connector
            .request(new Contracts.ActorAwaitReq(requestId, 350))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorAwaitRes.class);
        Thread.sleep(75);
        CompletionStage<Contracts.ActorFastRes> fast = connector
            .request(new Contracts.ActorFastReq(requestId, "b2-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorFastRes.class);

        Contracts.ActorAwaitRes awaitReply = await.toCompletableFuture().join();
        Contracts.ActorFastRes fastReply = fast.toCompletableFuture().join();
        ensure(actorA.equals(awaitReply.actorId()), "ATD-B2 await actor mismatch");
        ensure(actorA.equals(fastReply.actorId()), "ATD-B2 fast actor mismatch");
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        assertOrder(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "actor-await-resumed",
            "actor-await-completed",
            "actor-fast-started",
            "actor-fast-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "actor-await-resumed",
            "actor-await-completed",
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorA);
    }

    public static void runActorJoinAwait(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdb3-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsRes bind = connector
            .request(new Contracts.BindActorsReq(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.BindActorsRes.class).toCompletableFuture().join();
        ensure(actorA.equals(bind.actorA()), "ATD-B3 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "ATD-B3 actor B bind mismatch");

        CompletionStage<Contracts.ActorJoinAwaitRes> join = connector
            .request(new Contracts.ActorJoinAwaitReq(requestId, Contracts.TARGET_SPOT))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorJoinAwaitRes.class);
        Thread.sleep(75);
        CompletionStage<Contracts.ActorFastRes> fast = connector
            .request(new Contracts.ActorFastReq(requestId, "b3-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorB)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorFastRes.class);

        Contracts.ActorFastRes fastReply = fast.toCompletableFuture().join();
        Contracts.ActorJoinAwaitRes joinReply = join.toCompletableFuture().join();
        ensure(actorA.equals(joinReply.actorId()), "ATD-B3 join actor mismatch");
        ensure(actorB.equals(fastReply.actorId()), "ATD-B3 fast actor mismatch");
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        assertOrder(playEvidence, requestId, List.of(
            "actor-join-await-started",
            "actor-join-await-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-await-resumed",
            "actor-join-await-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-join-await-started",
            "actor-join-await-released",
            "actor-join-await-resumed",
            "actor-join-await-completed"), "actor=" + actorA);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorB);
    }

    public static void runTimerIsolation(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdc1-" + System.nanoTime();
        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        connector
            .send(new Contracts.TimerStartMsg(
                requestId,
                requestId + "-await",
                "await-on-first",
                50,
                ISOLATION_DELAY_MILLIS))
            .metadata(metadata)
            .submit();
        connector
            .send(new Contracts.TimerStartMsg(
                requestId,
                requestId + "-fast",
                "fast",
                100,
                0))
            .metadata(metadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-await-resumed",
            "timer-await-completed"));
        connector
            .send(new Contracts.TimerStopMsg(requestId))
            .metadata(metadata)
            .submit();
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released",
            "timer-await-resumed",
            "timer-await-completed"), "timer=" + requestId + "-await");
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-fast-started",
            "timer-fast-completed"), "timer=" + requestId + "-fast");
    }

    public static void runSameTimerReentry(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdc2-" + System.nanoTime();
        String timerName = requestId + "-same";
        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        connector
            .send(new Contracts.TimerStartMsg(
                requestId,
                timerName,
                "await-then-next",
                50,
                1000))
            .metadata(metadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released",
            "timer-await-resumed",
            "timer-await-completed",
            "timer-next-started",
            "timer-next-completed"));
        connector
            .send(new Contracts.TimerStopMsg(requestId))
            .metadata(metadata)
            .submit();
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released",
            "timer-await-resumed",
            "timer-await-completed",
            "timer-next-started",
            "timer-next-completed"), "timer=" + timerName);
    }

    public static void runActorTimerIsolation(ZLinkStreamConnector connector) throws Exception {
        String scenarioId = "atdc3-" + System.nanoTime();
        String actorA = scenarioId + "-actor-a";
        String actorB = scenarioId + "-actor-b";
        Contracts.BindActorsRes bind = connector
            .request(new Contracts.BindActorsReq(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.BindActorsRes.class).toCompletableFuture().join();
        ensure(actorA.equals(bind.actorA()), "ATD-C3 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "ATD-C3 actor B bind mismatch");

        joinActor(connector, scenarioId + "-join-a", actorA);
        joinActor(connector, scenarioId + "-join-b", actorB);

        runActorThenTimer(connector, scenarioId + "-actor-then-timer", actorA);
        runTimerThenActor(connector, scenarioId + "-timer-then-actor", actorB);
    }

    private static void runActorThenTimer(
        ZLinkStreamConnector connector,
        String requestId,
        String actorId) throws Exception {
        Map<String, String> timerMetadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        String timerName = requestId + "-fast";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        CompletionStage<Contracts.ActorAwaitRes> actorAwait = connector
            .request(new Contracts.ActorAwaitReq(requestId, ACTOR_TIMER_ISOLATION_DELAY_MILLIS))
            .metadata(Contracts.ACTOR_ID_METADATA, actorId)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorAwaitRes.class);
        assertOrder(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released"));
        connector
            .send(new Contracts.TimerStartMsg(
                requestId,
                timerName,
                "fast",
                50,
                0))
            .metadata(timerMetadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "timer-fast-started",
            "timer-fast-completed"));
        connector
            .send(new Contracts.TimerStopMsg(requestId))
            .metadata(timerMetadata)
            .submit();
        Contracts.ActorAwaitRes reply = actorAwait.toCompletableFuture().join();
        ensure(actorId.equals(reply.actorId()), "ATD-C3 actor await reply mismatch");
        assertOrder(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-await-resumed",
            "actor-await-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-await-started",
            "actor-await-released",
            "actor-await-resumed",
            "actor-await-completed"), "actor=" + actorId);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-fast-started",
            "timer-fast-completed"), "timer=" + timerName);
    }

    private static void runTimerThenActor(
        ZLinkStreamConnector connector,
        String requestId,
        String actorId) throws Exception {
        Map<String, String> timerMetadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        String timerName = requestId + "-await";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        connector
            .send(new Contracts.TimerStartMsg(
                requestId,
                timerName,
                "await-on-first",
                50,
                ACTOR_TIMER_ISOLATION_DELAY_MILLIS))
            .metadata(timerMetadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released"));
        Contracts.ActorFastRes reply = connector
            .request(new Contracts.ActorFastReq(requestId, "c3-actor-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorId)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorFastRes.class).toCompletableFuture().join();
        ensure(actorId.equals(reply.actorId()), "ATD-C3 actor fast reply mismatch");
        assertOrder(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-await-resumed",
            "timer-await-completed"));
        connector
            .send(new Contracts.TimerStopMsg(requestId))
            .metadata(timerMetadata)
            .submit();
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-await-started",
            "timer-await-released",
            "timer-await-resumed",
            "timer-await-completed"), "timer=" + timerName);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorId);
    }

    public static void runRemoteSpotAwait(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdd2-" + System.nanoTime();
        String ownerSpot = requestId + "-owner";
        String targetSpot = requestId + "-target";
        String playAEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        String playBEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_B_HTTP") + "/evidence";
        Contracts.EnsureSpotRes owner = connector
            .request(new Contracts.EnsureSpotReq(ownerSpot))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(ownerSpot.equals(owner.spotRid()), "ATD-D2 owner spot mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(owner.nodeRid()), "ATD-D2 owner node mismatch");
        Contracts.EnsureSpotRes target = connector
            .request(new Contracts.EnsureSpotReq(targetSpot))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(targetSpot.equals(target.spotRid()), "ATD-D2 target spot mismatch");
        ensure(Contracts.PLAY_NODE_B.equals(target.nodeRid()), "ATD-D2 target node mismatch");

        Contracts.ScenarioRes reply = connector
            .request(new Contracts.RemoteSpotAwaitReq(requestId, targetSpot, 350))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .metadata(Contracts.SPOT_RID_METADATA, ownerSpot)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ScenarioRes.class).toCompletableFuture().join();
        ensure("ATD-D2".equals(reply.scenarioId()), "ATD-D2 reply scenario mismatch");
        ensure(requestId.equals(reply.requestId()), "ATD-D2 reply request mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(reply.result()), "ATD-D2 owner continuation node mismatch");

        assertOrder(playAEvidence, requestId, List.of(
            "remote-await-started",
            "remote-await-released",
            "remote-await-resumed",
            "remote-await-completed"));
        assertAllValuesContain(playAEvidence, requestId, List.of(
            "remote-await-started",
            "remote-await-released",
            "remote-await-resumed",
            "remote-await-completed"), "spot=" + ownerSpot);
        assertAllValuesContain(playAEvidence, requestId, List.of(
            "remote-await-resumed",
            "remote-await-completed"), "targetNode=" + Contracts.PLAY_NODE_B);
        assertOrder(playBEvidence, requestId, List.of(
            "await-started",
            "await-released",
            "await-resumed",
            "await-completed"));
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "await-started",
            "await-released",
            "await-resumed",
            "await-completed"), "spot=" + targetSpot);
        assertNoMarker(playBEvidence, requestId, "remote-await-resumed");
    }

    public static void runRouteBridgeAwait(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdd3-" + System.nanoTime();
        String spotRid = requestId + "-spot";
        String playBEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_B_HTTP") + "/evidence";
        Contracts.EnsureSpotRes target = connector
            .request(new Contracts.EnsureSpotReq(spotRid))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(spotRid.equals(target.spotRid()), "ATD-D3 target spot mismatch");
        ensure(Contracts.PLAY_NODE_B.equals(target.nodeRid()), "ATD-D3 target node mismatch");

        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, spotRid,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B);
        waitRouteBridgeSendReady(connector, playBEvidence, requestId + "-ready", metadata);
        connector
            .send(new Contracts.AwaitMsg(requestId, ISOLATION_DELAY_MILLIS, "route-bridge"))
            .metadata(metadata)
            .submit();
        assertOrder(playBEvidence, requestId, List.of(
            "await-started",
            "await-released"));
        connector
            .send(new Contracts.ProbeMsg(requestId, "route-bridge-probe"))
            .metadata(metadata)
            .submit();
        assertOrder(playBEvidence, requestId, List.of(
            "await-started",
            "await-released",
            "probe-started",
            "probe-completed",
            "await-resumed",
            "await-completed"));
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "await-started",
            "await-released",
            "await-resumed",
            "await-completed"), "spot=" + spotRid);
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "await-started",
            "await-released",
            "await-resumed",
            "await-completed"), "handler=spot");
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "probe-started",
            "probe-completed"), spotRid);
    }

    private static void waitRouteBridgeSendReady(
        ZLinkStreamConnector connector,
        String evidenceUrl,
        String requestId,
        Map<String, String> metadata) throws Exception {
        long deadline = System.nanoTime() + REQUEST_TIMEOUT.toNanos();
        int attempt = 0;
        while (System.nanoTime() < deadline) {
            String probeId = requestId + "-" + attempt++;
            connector
                .send(new Contracts.ProbeMsg(probeId, "route-bridge-ready"))
                .metadata(metadata)
                .submit();
            long probeDeadline = System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (System.nanoTime() < probeDeadline) {
                if (containsInOrder(
                    observedMarkers(evidenceUrl, probeId),
                    List.of("probe-started", "probe-completed"))) {
                    return;
                }
                Thread.sleep(100);
            }
        }
        throw new IllegalStateException("route bridge send path was not ready for " + requestId);
    }

    public static void runSessionRelayActorAwait(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atdd4-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        Contracts.BindActorsRes bind = connector
            .request(new Contracts.BindActorsReq(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.BindActorsRes.class).toCompletableFuture().join();
        ensure(actorA.equals(bind.actorA()), "ATD-D4 actor A bind mismatch");

        ZLinkStreamConnector unbound = ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions.createDefault(URI.create(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))));
        try {
            unbound.connect().submit().toCompletableFuture().join();
            CompletionStage<ZLinkStreamMessage<Contracts.ActorPushNotify>> push = connector
                .waitFor(Contracts.ActorPushNotify.class)
                .timeout(REQUEST_TIMEOUT)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorPushAwaitRes reply = connector
                .request(new Contracts.ActorPushAwaitReq(requestId, 350, "bound-session-push"))
                .metadata(Contracts.ACTOR_ID_METADATA, actorA)
                .timeout(REQUEST_TIMEOUT)
                .submit(Contracts.ActorPushAwaitRes.class).toCompletableFuture().join();
            connector.dispatch().submit().toCompletableFuture().join();
            Contracts.ActorPushNotify notify = push.toCompletableFuture().join().payload();
            ensure("ATD-D4".equals(reply.scenarioId()), "ATD-D4 reply scenario mismatch");
            ensure(actorA.equals(reply.actorId()), "ATD-D4 reply actor mismatch");
            ensure("actor-push-await-completed".equals(reply.marker()), "ATD-D4 reply marker mismatch");
            ensure(actorA.equals(notify.actorId()), "ATD-D4 push actor mismatch");
            ensure(requestId.equals(notify.requestId()), "ATD-D4 push request mismatch");
            ensure("bound-session-push".equals(notify.value()), "ATD-D4 push value mismatch");
            ensure(Contracts.PLAY_NODE_A.equals(notify.nodeRid()), "ATD-D4 push node mismatch");
            CompletionStage<ZLinkStreamMessage<Contracts.ActorPushNotify>> unboundPush = unbound
                .waitFor(Contracts.ActorPushNotify.class)
                .timeout(Duration.ofMillis(400))
                .submit(Contracts.ActorPushNotify.class);
            unbound.dispatch().submit().toCompletableFuture().join();
            expectFailure(() -> unboundPush.toCompletableFuture().join(), "ATD-D4 unbound session received actor push");
        } finally {
            unbound.close().submit().toCompletableFuture().join();
        }

        assertOrder(playEvidence, requestId, List.of(
            "actor-push-await-started",
            "actor-push-await-released",
            "actor-push-await-resumed",
            "actor-push-await-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-push-await-started",
            "actor-push-await-released",
            "actor-push-await-resumed",
            "actor-push-await-completed"), "actor=" + actorA);
    }

    public static void runTimeoutCleanup(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atde1-" + System.nanoTime();
        String spotRid = requestId + "-spot";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        Contracts.EnsureSpotRes target = connector
            .request(new Contracts.EnsureSpotReq(spotRid))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(spotRid.equals(target.spotRid()), "ATD-E1 spot mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(target.nodeRid()), "ATD-E1 node mismatch");

        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, spotRid,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A);
        connector
            .send(new Contracts.AwaitTimeoutMsg(requestId, 700, 100))
            .metadata(metadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "timeout-await-started",
            "timeout-await-released",
            "timeout-await-completed"));
        assertNoMarker(playEvidence, requestId, "timeout-await-unexpected-resumed");
        Contracts.ProbeRes timeoutProbe = connector
            .request(new Contracts.ProbeReq(requestId))
            .metadata(metadata)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ProbeRes.class).toCompletableFuture().join();
        ensure(requestId.equals(timeoutProbe.requestId()), "ATD-E1 probe reply mismatch");
        assertOrder(playEvidence, requestId, List.of(
            "timeout-await-started",
            "timeout-await-released",
            "timeout-await-completed",
            "probe-started",
            "probe-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timeout-await-started",
            "timeout-await-released",
            "timeout-await-completed"), "spot=" + spotRid);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "probe-started",
            "probe-completed"), spotRid);
    }

    public static void runCancellationCleanup(ZLinkStreamConnector connector) throws Exception {
        String requestId = "atde2-" + System.nanoTime();
        String spotRid = requestId + "-spot";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        Contracts.EnsureSpotRes target = connector
            .request(new Contracts.EnsureSpotReq(spotRid))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(spotRid.equals(target.spotRid()), "ATD-E2 spot mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(target.nodeRid()), "ATD-E2 node mismatch");

        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, spotRid,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A);
        connector
            .send(new Contracts.AwaitCancelMsg(requestId, 700, 100))
            .metadata(metadata)
            .submit();
        assertOrder(playEvidence, requestId, List.of(
            "cancel-await-started",
            "cancel-await-released",
            "cancel-await-completed"));
        assertNoMarker(playEvidence, requestId, "cancel-await-unexpected-resumed");
        Contracts.ProbeRes cancellationProbe = connector
            .request(new Contracts.ProbeReq(requestId))
            .metadata(metadata)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ProbeRes.class).toCompletableFuture().join();
        ensure(requestId.equals(cancellationProbe.requestId()), "ATD-E2 probe reply mismatch");
        assertOrder(playEvidence, requestId, List.of(
            "cancel-await-started",
            "cancel-await-released",
            "cancel-await-completed",
            "probe-started",
            "probe-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "cancel-await-started",
            "cancel-await-released",
            "cancel-await-completed"), "spot=" + spotRid);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "probe-started",
            "probe-completed"), spotRid);
    }

    public static void runShutdownWait(ZLinkStreamConnector connector) throws Exception {
        String requestId = Env.get("ZLINK_JAVA_E2E_SHUTDOWN_REQUEST_ID");
        String spotRid = Env.get("ZLINK_JAVA_E2E_SHUTDOWN_SPOT_RID");
        try {
            Contracts.AwaitShutdownRes result = connector
                .request(new Contracts.AwaitShutdownScenarioReq(requestId, spotRid, 30_000))
                .timeout(Duration.ofSeconds(90))
                .submit(Contracts.AwaitShutdownRes.class).toCompletableFuture().join();
            throw new IllegalStateException(
                "ATD-E3 expected play-a shutdown while await was pending, but request completed as "
                    + result.operation());
        } catch (RuntimeException error) {
            ensure(!String.valueOf(error.getMessage()).contains("unexpected-completion"),
                String.valueOf(error.getMessage()));
            System.out.println("automatic-turn-dispatch shutdown wait result=passed");
        }
    }

    public static void runShutdownRecovery(ZLinkStreamConnector connector) throws Exception {
        String requestId = Env.get("ZLINK_JAVA_E2E_SHUTDOWN_REQUEST_ID");
        String spotRid = Env.get("ZLINK_JAVA_E2E_SHUTDOWN_SPOT_RID");
        Contracts.AwaitShutdownRes result = connector
            .request(new Contracts.AwaitShutdownRecoveryReq(requestId, spotRid))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.AwaitShutdownRes.class).toCompletableFuture().join();
        ensure("atd.e3-shutdown-recovery".equals(result.operation()), "ATD-E3 recovery operation mismatch");
        ensure(requestId.equals(result.requestId()), "ATD-E3 recovery request mismatch");
        ensure(spotRid.equals(result.spotRid()), "ATD-E3 recovery spot mismatch");
        assertOrder(Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence", requestId, List.of(
            "probe-started",
            "probe-completed"));
        System.out.println("automatic-turn-dispatch shutdown recovery result=passed");
    }

    public static void runReadinessProbe(ZLinkStreamConnector connector) throws Exception {
        Contracts.EnsureSpotRes playA = connector
            .request(new Contracts.EnsureSpotReq(Contracts.TARGET_SPOT))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(Contracts.TARGET_SPOT.equals(playA.spotRid()), "readiness play-a spot mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(playA.nodeRid()), "readiness play-a node mismatch");

        String playBSpot = Contracts.TARGET_SPOT + "-readiness-b";
        Contracts.EnsureSpotRes playB = connector
            .request(new Contracts.EnsureSpotReq(playBSpot))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.EnsureSpotRes.class).toCompletableFuture().join();
        ensure(playBSpot.equals(playB.spotRid()), "readiness play-b spot mismatch");
        ensure(Contracts.PLAY_NODE_B.equals(playB.nodeRid()), "readiness play-b node mismatch");
    }

    private static void joinActor(
        ZLinkStreamConnector connector,
        String requestId,
        String actorId) throws Exception {
        Contracts.ActorJoinRes reply = connector
            .request(new Contracts.ActorJoinReq(requestId, Contracts.TARGET_SPOT))
            .metadata(Contracts.ACTOR_ID_METADATA, actorId)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
        ensure(actorId.equals(reply.actorId()), "ATD-B1 join actor mismatch");
        ensure("joined".equals(reply.marker()), "ATD-B1 join marker mismatch");
    }

    private static void runScenario(
        ZLinkStreamConnector connector,
        String scenarioId,
        List<String> expectedOrder) throws Exception {
        runScenario(connector, scenarioId, expectedOrder, Map.of(), List.of());
    }

    private static void runScenario(
        ZLinkStreamConnector connector,
        String scenarioId,
        List<String> expectedOrder,
        Map<String, String> metadata,
        List<String> expectedValueFragments) throws Exception {
        String requestId = scenarioId.toLowerCase().replace("-", "") + "-" + System.nanoTime();
        Contracts.ScenarioRes reply = connector
            .request(new Contracts.ScenarioReq(scenarioId, requestId))
            .metadata(metadata)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ScenarioRes.class).toCompletableFuture().join();
        ensure(scenarioId.equals(reply.scenarioId()), scenarioId + " reply scenario mismatch");
        ensure(requestId.equals(reply.requestId()), scenarioId + " reply request id mismatch");
        assertOrder(requestId, expectedOrder);
        for (String valueFragment : expectedValueFragments) {
            assertAllValuesContain(requestId, expectedOrder, valueFragment);
        }
    }

    private static void assertOrder(String requestId, List<String> expectedOrder) throws Exception {
        assertOrder(Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence", requestId, expectedOrder);
    }

    private static void assertOrder(String evidenceUrl, String requestId, List<String> expectedOrder)
        throws Exception {
        long deadline = System.nanoTime() + REQUEST_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            List<String> observed = observedMarkers(evidenceUrl, requestId);
            if (containsInOrder(observed, expectedOrder)) {
                return;
            }
            Thread.sleep(100);
        }
        throw new IllegalStateException(
            "expected marker order " + expectedOrder + " for " + requestId
                + ", observed=" + observedMarkers(evidenceUrl, requestId));
    }

    private static List<String> observedMarkers(String requestId) throws Exception {
        return observedMarkers(Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence", requestId);
    }

    private static List<String> observedMarkers(String evidenceUrl, String requestId) throws Exception {
        JsonNode root = JSON.readTree(get(evidenceUrl));
        List<String> markers = new ArrayList<>();
        for (JsonNode entry : root.path("entries")) {
            if (requestId.equals(entry.path("subject").asText())) {
                markers.add(entry.path("marker").asText());
            }
        }
        return markers;
    }

    private static void assertAllValuesContain(
        String requestId,
        List<String> expectedMarkers,
        String valueFragment) throws Exception {
        assertAllValuesContain(
            Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence",
            requestId,
            expectedMarkers,
            valueFragment);
    }

    private static void assertAllValuesContain(
        String evidenceUrl,
        String requestId,
        List<String> expectedMarkers,
        String valueFragment) throws Exception {
        JsonNode root = JSON.readTree(get(evidenceUrl));
        for (JsonNode entry : root.path("entries")) {
            String marker = entry.path("marker").asText();
            if (requestId.equals(entry.path("subject").asText()) && expectedMarkers.contains(marker)) {
                String value = entry.path("value").asText();
                ensure(value.contains(valueFragment),
                    "expected " + marker + " value to contain " + valueFragment + ", value=" + value);
            }
        }
    }

    private static void assertNoMarker(String evidenceUrl, String requestId, String marker) throws Exception {
        List<String> observed = observedMarkers(evidenceUrl, requestId);
        ensure(!observed.contains(marker), "unexpected marker " + marker + " for " + requestId);
    }

    private static void waitForMetricValue(String metricsUrl, String metricName, double minimum)
        throws Exception {
        long deadline = System.nanoTime() + REQUEST_TIMEOUT.toNanos();
        while (System.nanoTime() < deadline) {
            JsonNode rows = JSON.readTree(get(metricsUrl));
            for (JsonNode row : rows) {
                if (metricName.equals(row.path("name").asText())
                    && row.path("value").asDouble() >= minimum) {
                    return;
                }
            }
            Thread.sleep(100);
        }
        throw new IllegalStateException(
            "timed out waiting for " + metricName + " >= " + minimum);
    }

    private interface ThrowingRunnable {
        void run() throws Exception;
    }

    private static void expectFailure(ThrowingRunnable action, String message) throws Exception {
        try {
            action.run();
        } catch (Exception error) {
            if (error instanceof ExecutionException && error.getCause() != null) {
                return;
            }
            return;
        }
        throw new IllegalStateException(message);
    }

    private static boolean containsInOrder(List<String> observed, List<String> expected) {
        int index = 0;
        for (String marker : observed) {
            if (index < expected.size() && expected.get(index).equals(marker)) {
                index++;
            }
        }
        return index == expected.size();
    }

    private static String get(String url) throws Exception {
        URI target = URI.create(url);
        String baseUrl = target.getScheme() + "://" + target.getRawAuthority();
        String path = target.getRawPath();
        if (target.getRawQuery() != null) {
            path += "?" + target.getRawQuery();
        }
        RawHttpResponse response = ZLinkHttpClient.create(baseUrl)
            .timeout(Duration.ofSeconds(3))
            .get(path)
            .submitRaw()
            .toCompletableFuture()
            .join();
        ensure(response.status() >= 200 && response.status() < 300,
            "GET " + url + " returned " + response.status());
        return response.body();
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
