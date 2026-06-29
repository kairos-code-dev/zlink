package systems.zlink.e2e.yielddispatch.client;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ExecutionException;
import systems.zlink.e2e.yielddispatch.shared.Contracts;
import systems.zlink.e2e.yielddispatch.shared.Env;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class Program {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(30);
    private static final ObjectMapper JSON = new ObjectMapper();

    private Program() {
    }

    public static void main(String... args) throws Exception {
        ZLinkStreamConnector connector = ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions.createDefault(URI.create(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))));
        try {
            connector.connect().await();
            runScenario(connector, "YD-A1", List.of(
                "hold-started",
                "hold-resumed",
                "hold-completed",
                "probe-started"));
            System.out.println("scenario YD-A1 passed");
            runScenario(connector, "YD-A2", List.of(
                "yield-started",
                "yield-released",
                "probe-started",
                "probe-completed",
                "yield-resumed",
                "yield-completed"));
            System.out.println("scenario YD-A2 passed");
            runScenario(connector, "YD-A3", List.of(
                    "yield-started",
                    "yield-released",
                    "yield-resumed",
                    "yield-completed"),
                Map.of(
                    Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
                    Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE),
                List.of(
                    "spot=" + Contracts.TARGET_SPOT,
                    "correlation=corr-a3"));
            System.out.println("scenario YD-A3 passed");
            runWorkerYield(connector);
            System.out.println("scenario YD-A4 passed");
            runActorOtherProgress(connector);
            System.out.println("scenario YD-B1 passed");
            runSameActorReentry(connector);
            System.out.println("scenario YD-B2 passed");
            runActorJoinYield(connector);
            System.out.println("scenario YD-B3 passed");
            runTimerIsolation(connector);
            System.out.println("scenario YD-C1 passed");
            runSameTimerReentry(connector);
            System.out.println("scenario YD-C2 passed");
            runActorTimerIsolation(connector);
            System.out.println("scenario YD-C3 passed");
            runRemoteSpotYield(connector);
            System.out.println("scenario YD-D2 passed");
            runRouteBridgeYield(connector);
            System.out.println("scenario YD-D3 passed");
            runSessionRelayActorYield(connector);
            System.out.println("scenario YD-D4 passed");
            System.out.println("yield-dispatch e2e result=passed");
        } finally {
            connector.close().await();
        }
    }

    private static void runActorOtherProgress(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydb1-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsReply bind = connector
            .request(new Contracts.BindActorsRequest(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.BindActorsReply.class);
        ensure(actorA.equals(bind.actorA()), "YD-B1 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "YD-B1 actor B bind mismatch");

        joinActor(connector, requestId + "-join-a", actorA);
        joinActor(connector, requestId + "-join-b", actorB);

        CompletionStage<Contracts.ActorReply> yield = connector
            .request(new Contracts.ActorYieldRequest(requestId, 800))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);
        Thread.sleep(150);
        CompletionStage<Contracts.ActorReply> fast = connector
            .request(new Contracts.ActorFastRequest(requestId, "b1-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorB)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);

        Contracts.ActorReply fastReply = connector.await(fast);
        Contracts.ActorReply yieldReply = connector.await(yield);
        ensure(actorA.equals(yieldReply.actorId()), "YD-B1 yield actor mismatch");
        ensure(actorB.equals(fastReply.actorId()), "YD-B1 fast actor mismatch");
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        assertOrder(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed"), "actor=" + actorA);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorB);
    }

    private static void runWorkerYield(ZLinkStreamConnector connector) throws Exception {
        String requestId = "yda4-" + System.nanoTime();
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        connector
            .send(new Contracts.WorkerYieldCommand(requestId, 1200))
            .metadata(metadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "worker-yield-started",
            "worker-yield-released"));
        connector
            .send(new Contracts.ProbeCommand(requestId, "worker-probe"))
            .metadata(metadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "worker-yield-started",
            "worker-yield-released",
            "probe-started",
            "probe-completed",
            "worker-yield-resumed",
            "worker-yield-completed"));
    }

    private static void runSameActorReentry(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydb2-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsReply bind = connector
            .request(new Contracts.BindActorsRequest(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.BindActorsReply.class);
        ensure(actorA.equals(bind.actorA()), "YD-B2 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "YD-B2 actor B bind mismatch");

        joinActor(connector, requestId + "-join-a", actorA);

        CompletionStage<Contracts.ActorReply> yield = connector
            .request(new Contracts.ActorYieldRequest(requestId, 350))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);
        Thread.sleep(75);
        CompletionStage<Contracts.ActorReply> fast = connector
            .request(new Contracts.ActorFastRequest(requestId, "b2-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);

        Contracts.ActorReply yieldReply = connector.await(yield);
        Contracts.ActorReply fastReply = connector.await(fast);
        ensure(actorA.equals(yieldReply.actorId()), "YD-B2 yield actor mismatch");
        ensure(actorA.equals(fastReply.actorId()), "YD-B2 fast actor mismatch");
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        assertOrder(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed",
            "actor-fast-started",
            "actor-fast-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed",
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorA);
    }

    private static void runActorJoinYield(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydb3-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        Contracts.BindActorsReply bind = connector
            .request(new Contracts.BindActorsRequest(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.BindActorsReply.class);
        ensure(actorA.equals(bind.actorA()), "YD-B3 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "YD-B3 actor B bind mismatch");

        CompletionStage<Contracts.ActorReply> join = connector
            .request(new Contracts.ActorJoinYieldRequest(requestId, Contracts.TARGET_SPOT))
            .metadata(Contracts.ACTOR_ID_METADATA, actorA)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);
        Thread.sleep(75);
        CompletionStage<Contracts.ActorReply> fast = connector
            .request(new Contracts.ActorFastRequest(requestId, "b3-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorB)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);

        Contracts.ActorReply fastReply = connector.await(fast);
        Contracts.ActorReply joinReply = connector.await(join);
        ensure(actorA.equals(joinReply.actorId()), "YD-B3 join actor mismatch");
        ensure(actorB.equals(fastReply.actorId()), "YD-B3 fast actor mismatch");
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        assertOrder(playEvidence, requestId, List.of(
            "actor-join-yield-started",
            "actor-join-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "actor-join-yield-resumed",
            "actor-join-yield-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-join-yield-started",
            "actor-join-yield-released",
            "actor-join-yield-resumed",
            "actor-join-yield-completed"), "actor=" + actorA);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorB);
    }

    private static void runTimerIsolation(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydc1-" + System.nanoTime();
        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        connector
            .send(new Contracts.TimerStartCommand(
                requestId,
                requestId + "-yield",
                "yield-on-first",
                50,
                1000))
            .metadata(metadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released"));
        connector
            .send(new Contracts.TimerStartCommand(
                requestId,
                requestId + "-fast",
                "fast",
                50,
                0))
            .metadata(metadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"));
        connector
            .send(new Contracts.TimerStopCommand(requestId))
            .metadata(metadata)
            .await();
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed"), "timer=" + requestId + "-yield");
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-fast-started",
            "timer-fast-completed"), "timer=" + requestId + "-fast");
    }

    private static void runSameTimerReentry(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydc2-" + System.nanoTime();
        String timerName = requestId + "-same";
        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, Contracts.TARGET_SPOT,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE);
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        connector
            .send(new Contracts.TimerStartCommand(
                requestId,
                timerName,
                "yield-then-next",
                50,
                1000))
            .metadata(metadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"));
        connector
            .send(new Contracts.TimerStopCommand(requestId))
            .metadata(metadata)
            .await();
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed",
            "timer-next-started",
            "timer-next-completed"), "timer=" + timerName);
    }

    private static void runActorTimerIsolation(ZLinkStreamConnector connector) throws Exception {
        String scenarioId = "ydc3-" + System.nanoTime();
        String actorA = scenarioId + "-actor-a";
        String actorB = scenarioId + "-actor-b";
        Contracts.BindActorsReply bind = connector
            .request(new Contracts.BindActorsRequest(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.BindActorsReply.class);
        ensure(actorA.equals(bind.actorA()), "YD-C3 actor A bind mismatch");
        ensure(actorB.equals(bind.actorB()), "YD-C3 actor B bind mismatch");

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
        CompletionStage<Contracts.ActorReply> actorYield = connector
            .request(new Contracts.ActorYieldRequest(requestId, 350))
            .metadata(Contracts.ACTOR_ID_METADATA, actorId)
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.ActorReply.class);
        assertOrder(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released"));
        connector
            .send(new Contracts.TimerStartCommand(
                requestId,
                timerName,
                "fast",
                50,
                0))
            .metadata(timerMetadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed"));
        connector
            .send(new Contracts.TimerStopCommand(requestId))
            .metadata(timerMetadata)
            .await();
        Contracts.ActorReply reply = connector.await(actorYield);
        ensure(actorId.equals(reply.actorId()), "YD-C3 actor yield reply mismatch");
        assertOrder(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "timer-fast-started",
            "timer-fast-completed",
            "actor-yield-resumed",
            "actor-yield-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-yield-started",
            "actor-yield-released",
            "actor-yield-resumed",
            "actor-yield-completed"), "actor=" + actorId);
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
        String timerName = requestId + "-yield";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        connector
            .send(new Contracts.TimerStartCommand(
                requestId,
                timerName,
                "yield-on-first",
                50,
                1000))
            .metadata(timerMetadata)
            .await();
        assertOrder(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released"));
        Contracts.ActorReply reply = connector
            .request(new Contracts.ActorFastRequest(requestId, "c3-actor-fast"))
            .metadata(Contracts.ACTOR_ID_METADATA, actorId)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.ActorReply.class);
        ensure(actorId.equals(reply.actorId()), "YD-C3 actor fast reply mismatch");
        assertOrder(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "actor-fast-started",
            "actor-fast-completed",
            "timer-yield-resumed",
            "timer-yield-completed"));
        connector
            .send(new Contracts.TimerStopCommand(requestId))
            .metadata(timerMetadata)
            .await();
        assertAllValuesContain(playEvidence, requestId, List.of(
            "timer-yield-started",
            "timer-yield-released",
            "timer-yield-resumed",
            "timer-yield-completed"), "timer=" + timerName);
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-fast-started",
            "actor-fast-completed"), "actor=" + actorId);
    }

    private static void runRemoteSpotYield(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydd2-" + System.nanoTime();
        String ownerSpot = requestId + "-owner";
        String targetSpot = requestId + "-target";
        String playAEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        String playBEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_B_HTTP") + "/evidence";
        Contracts.EnsureSpotReply owner = connector
            .request(new Contracts.EnsureSpotRequest(ownerSpot))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EnsureSpotReply.class);
        ensure(ownerSpot.equals(owner.spotRid()), "YD-D2 owner spot mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(owner.nodeRid()), "YD-D2 owner node mismatch");
        Contracts.EnsureSpotReply target = connector
            .request(new Contracts.EnsureSpotRequest(targetSpot))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EnsureSpotReply.class);
        ensure(targetSpot.equals(target.spotRid()), "YD-D2 target spot mismatch");
        ensure(Contracts.PLAY_NODE_B.equals(target.nodeRid()), "YD-D2 target node mismatch");

        Contracts.ScenarioReply reply = connector
            .request(new Contracts.RemoteSpotYieldRequest(requestId, targetSpot, 350))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_A)
            .metadata(Contracts.SPOT_RID_METADATA, ownerSpot)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.ScenarioReply.class);
        ensure("YD-D2".equals(reply.scenarioId()), "YD-D2 reply scenario mismatch");
        ensure(requestId.equals(reply.requestId()), "YD-D2 reply request mismatch");
        ensure(Contracts.PLAY_NODE_A.equals(reply.result()), "YD-D2 owner continuation node mismatch");

        assertOrder(playAEvidence, requestId, List.of(
            "remote-yield-started",
            "remote-yield-released",
            "remote-yield-resumed",
            "remote-yield-completed"));
        assertAllValuesContain(playAEvidence, requestId, List.of(
            "remote-yield-started",
            "remote-yield-released",
            "remote-yield-resumed",
            "remote-yield-completed"), "spot=" + ownerSpot);
        assertAllValuesContain(playAEvidence, requestId, List.of(
            "remote-yield-resumed",
            "remote-yield-completed"), "targetNode=" + Contracts.PLAY_NODE_B);
        assertOrder(playBEvidence, requestId, List.of(
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"));
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"), "spot=" + targetSpot);
        assertNoMarker(playBEvidence, requestId, "remote-yield-resumed");
    }

    private static void runRouteBridgeYield(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydd3-" + System.nanoTime();
        String spotRid = requestId + "-spot";
        String playBEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_B_HTTP") + "/evidence";
        Contracts.EnsureSpotReply target = connector
            .request(new Contracts.EnsureSpotRequest(spotRid))
            .metadata(Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.EnsureSpotReply.class);
        ensure(spotRid.equals(target.spotRid()), "YD-D3 target spot mismatch");
        ensure(Contracts.PLAY_NODE_B.equals(target.nodeRid()), "YD-D3 target node mismatch");

        Map<String, String> metadata = Map.of(
            Contracts.SPOT_RID_METADATA, spotRid,
            Contracts.TARGET_NODE_RID_METADATA, Contracts.PLAY_NODE_B);
        connector
            .send(new Contracts.YieldCommand(requestId, 1000, "route-bridge"))
            .metadata(metadata)
            .await();
        assertOrder(playBEvidence, requestId, List.of(
            "yield-started",
            "yield-released"));
        connector
            .send(new Contracts.ProbeCommand(requestId, "route-bridge-probe"))
            .metadata(metadata)
            .await();
        assertOrder(playBEvidence, requestId, List.of(
            "yield-started",
            "yield-released",
            "probe-started",
            "probe-completed",
            "yield-resumed",
            "yield-completed"));
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"), "spot=" + spotRid);
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "yield-started",
            "yield-released",
            "yield-resumed",
            "yield-completed"), "handler=spot");
        assertAllValuesContain(playBEvidence, requestId, List.of(
            "probe-started",
            "probe-completed"), spotRid);
    }

    private static void runSessionRelayActorYield(ZLinkStreamConnector connector) throws Exception {
        String requestId = "ydd4-" + System.nanoTime();
        String actorA = requestId + "-actor-a";
        String actorB = requestId + "-actor-b";
        String playEvidence = Env.get("ZLINK_JAVA_E2E_PLAY_HTTP") + "/evidence";
        Contracts.BindActorsReply bind = connector
            .request(new Contracts.BindActorsRequest(Contracts.TARGET_SPOT, actorA, actorB))
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.BindActorsReply.class);
        ensure(actorA.equals(bind.actorA()), "YD-D4 actor A bind mismatch");

        ZLinkStreamConnector unbound = ZLinkStreamConnectorFactory.create(
            ZLinkStreamConnectorOptions.createDefault(URI.create(Env.get("ZLINK_JAVA_E2E_STREAM_ENDPOINT"))));
        try {
            unbound.connect().await();
            CompletionStage<ZLinkStreamMessage<Contracts.ActorPushNotify>> unboundPush = unbound
                .waitFor(Contracts.ActorPushNotify.class)
                .timeout(Duration.ofMillis(400))
                .submit(Contracts.ActorPushNotify.class);
            CompletionStage<ZLinkStreamMessage<Contracts.ActorPushNotify>> push = connector
                .waitFor(Contracts.ActorPushNotify.class)
                .timeout(REQUEST_TIMEOUT)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorReply reply = connector
                .request(new Contracts.ActorPushYieldRequest(requestId, 350, "bound-session-push"))
                .metadata(Contracts.ACTOR_ID_METADATA, actorA)
                .timeout(REQUEST_TIMEOUT)
                .await(Contracts.ActorReply.class);
            connector.dispatch().await();
            Contracts.ActorPushNotify notify = connector.await(push).payload();
            ensure("YD-D4".equals(reply.scenarioId()), "YD-D4 reply scenario mismatch");
            ensure(actorA.equals(reply.actorId()), "YD-D4 reply actor mismatch");
            ensure("actor-push-yield-completed".equals(reply.marker()), "YD-D4 reply marker mismatch");
            ensure(actorA.equals(notify.actorId()), "YD-D4 push actor mismatch");
            ensure(requestId.equals(notify.requestId()), "YD-D4 push request mismatch");
            ensure("bound-session-push".equals(notify.value()), "YD-D4 push value mismatch");
            ensure(Contracts.PLAY_NODE_A.equals(notify.nodeRid()), "YD-D4 push node mismatch");
            unbound.dispatch().await();
            expectFailure(() -> unbound.await(unboundPush), "YD-D4 unbound session received actor push");
        } finally {
            unbound.close().await();
        }

        assertOrder(playEvidence, requestId, List.of(
            "actor-push-yield-started",
            "actor-push-yield-released",
            "actor-push-yield-resumed",
            "actor-push-yield-completed"));
        assertAllValuesContain(playEvidence, requestId, List.of(
            "actor-push-yield-started",
            "actor-push-yield-released",
            "actor-push-yield-resumed",
            "actor-push-yield-completed"), "actor=" + actorA);
    }

    private static void joinActor(
        ZLinkStreamConnector connector,
        String requestId,
        String actorId) throws Exception {
        Contracts.ActorReply reply = connector
            .request(new Contracts.ActorJoinRequest(requestId, Contracts.TARGET_SPOT))
            .metadata(Contracts.ACTOR_ID_METADATA, actorId)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.ActorReply.class);
        ensure(actorId.equals(reply.actorId()), "YD-B1 join actor mismatch");
        ensure("joined".equals(reply.marker()), "YD-B1 join marker mismatch");
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
        Contracts.ScenarioReply reply = connector
            .request(new Contracts.ScenarioRequest(scenarioId, requestId))
            .metadata(metadata)
            .timeout(REQUEST_TIMEOUT)
            .await(Contracts.ScenarioReply.class);
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
        HttpResponse<String> response = HttpClient.newHttpClient().send(
            HttpRequest.newBuilder(URI.create(url))
                .timeout(Duration.ofSeconds(3))
                .GET()
                .build(),
            HttpResponse.BodyHandlers.ofString());
        ensure(response.statusCode() >= 200 && response.statusCode() < 300,
            "GET " + url + " returned " + response.statusCode());
        return response.body();
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
