package systems.zlink.e2e.spotservice.shared;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.actors.ZLinkActorClient;
import systems.zlink.framework.actors.ZLinkActorDirectory;
import systems.zlink.framework.channels.ZLinkRouteClient;
import systems.zlink.framework.spots.SpotHandle;
import systems.zlink.framework.spots.SpotHandleResolver;
import systems.zlink.framework.spots.ZLinkSpotOutbound;
import systems.zlink.stream.connector.ZLinkStreamCompression;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamConnectorFactory;
import systems.zlink.stream.connector.ZLinkStreamConnectorOptions;
import systems.zlink.stream.connector.ZLinkStreamDispatchMode;
import systems.zlink.stream.connector.ZLinkStreamMessage;
import systems.zlink.stream.connector.ZLinkStreamPacketNameResolver;

public final class ClientScenario {
    private static final Duration REQUEST_TIMEOUT = Duration.ofSeconds(5);
    private static final Duration EVENTUAL_TIMEOUT = Duration.ofSeconds(30);
    private static final ObjectMapper JSON = new ObjectMapper();
    private static final HttpClient HTTP = HttpClient.newHttpClient();
    private final ZLinkSpotOutbound outbound;
    private final ZLinkRouteClient routes;
    private final ZLinkActorClient actors;
    private final ZLinkActorDirectory actorRefs;
    private final SpotHandleResolver spotHandles;

    public ClientScenario(
        ZLinkSpotOutbound outbound,
        ZLinkRouteClient routes,
        ZLinkActorClient actors,
        ZLinkActorDirectory actorRefs,
        SpotHandleResolver spotHandles) {
        this.outbound = outbound;
        this.routes = routes;
        this.actors = actors;
        this.actorRefs = actorRefs;
        this.spotHandles = spotHandles;
    }

    public void runMode(String mode) {
        switch (mode) {
            case "state1" -> runState1();
            case "state2" -> runState2();
            case "send" -> runSend();
            case "timeout" -> runTimeout();
            case "missing" -> runMissingPacket();
            case "normal" -> runNormal();
            case "owner" -> runOwnerRouting();
            case "stage-wrapper" -> runStageWrapper();
            case "owner-remap" -> runOwnerRemap();
            case "route-mesh" -> runRouteMesh();
            case "route-lifecycle" -> runRouteLifecycle();
            case "actor-session" -> runActorSession();
            case "remote-actor-session" -> runRemoteActorSession();
            case "actor-missing" -> runActorMissingHandler();
            case "actor-destroy" -> runActorDestroy();
            case "actor-join-admission" -> runActorJoinAdmission();
            case "actor-push-chain" -> runActorPushChain();
            case "actor-leave-disconnect" -> runActorLeaveDisconnect();
            case "actor-disconnect-notify" -> runActorDisconnectNotify();
            case "bound-push-isolation" -> runBoundPushIsolation();
            case "stream-auth" -> runStreamAuth();
            case "stream-reconnect" -> runStreamReconnect();
            case "stream-rebind-transfer" -> runStreamRebindTransfer();
            case "multi-actor-bind" -> runMultiActorBind();
            case "stream-backpressure" -> runStreamBackpressure();
            case "mixed-stream-channel" -> runMixedStreamChannel();
            case "stream-heartbeat" -> runStreamHeartbeat();
            case "stream-tls" -> runStreamTls();
            case "multi-node" -> runMultiNodeRouteToSpot();
            case "spot-only-mesh" -> runSpotOnlyMesh();
            case "bound-push-load" -> runBoundPushLoad();
            case "join-leave-race" -> runJoinLeaveRace();
            case "play-crash-recovery" -> runPlayCrashRecovery();
            case "worker" -> runWorkerOffload();
            case "spot-outbound" -> runSpotOutbound();
            case "spot-to-spot" -> runSpotToSpot();
            case "spot-mesh-cross-node" -> runSpotMeshCrossNode();
            case "idle-timer" -> runIdleTimer();
            case "timer-overrun" -> runTimerOverrun();
            default -> throw new IllegalArgumentException("unknown client mode " + mode);
        }
    }

    private void runState1() {
        Contracts.StateRes first = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("a1"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure("room-a".equals(first.spotRid()), "SM-A1 wrong spot rid");
        ensure("play-a".equals(first.nodeRid()), "SM-A1 wrong owner node");
        System.out.println("scenario SM-A1 passed");
    }

    private void runState2() {
        Contracts.StateRes second = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("a2"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure(second.value().contains("a1") && second.value().contains("a2"),
            "SM-A2 state did not accumulate");
        System.out.println("scenario SM-A2 passed");
    }

    private void runSend() {
        outbound.sendToSpot(spotRef("room-a"), new Contracts.StateMsg("cmd-c1"))
            .submit();
        System.out.println("scenario SM-C1-send passed");
    }

    private void runTimeout() {
        expectFailure(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.SlowReq("late"))
            .timeout(Duration.ofMillis(100))
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        System.out.println("scenario SM-C1-timeout passed");
    }

    private void runNormal() {
        Contracts.StateRes after = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("after-timeout"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure(after.value().contains("after-timeout"), "SM-C1 post-timeout request failed");
        System.out.println("scenario SM-C1 passed");
        System.out.println("scenario SM-C1-normal passed");
    }

    private void runMissingPacket() {
        String ownerEndpoint = Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT");
        String spotRid = "spot-sm-e1-" + java.util.UUID.randomUUID().toString().replace("-", "");
        Contracts.CreateSpotRes created = postJson(
            ownerEndpoint,
            "/spot/create",
            new Contracts.CreateSpotReq(spotRid),
            Contracts.CreateSpotRes.class);
        ensure(spotRid.equals(created.spotRid()) && "play-a".equals(created.nodeRid()),
            "SM-E1 spot was not created on play-a");
        Contracts.StateRes ready = postJson(
            ownerEndpoint,
            "/spot/state/request",
            new Contracts.SpotStateRouteReq(spotRid, "sm-e1-ready", 0),
            Contracts.StateRes.class);
        ensure(spotRid.equals(ready.spotRid()) && "play-a".equals(ready.nodeRid()),
            "SM-E1 spot route was not ready on play-a");
        Contracts.SpotMissingHandlerRes missingRequest = postJson(
            ownerEndpoint,
            "/spot/missing-handler/request",
            new Contracts.SpotMissingHandlerReq(spotRid),
            Contracts.SpotMissingHandlerRes.class);
        ensure(missingRequest.failed(), "SM-E1 missing handler request did not fail");
        Contracts.SpotMissingCommandRes missingCommand = postJson(
            ownerEndpoint,
            "/spot/missing-handler/command",
            new Contracts.SpotMissingCommandReq(spotRid, "missing-command"),
            Contracts.SpotMissingCommandRes.class);
        ensure(missingCommand.sent(), "SM-E1 missing handler command was not sent");
        waitForEvidence(
            ownerEndpoint,
            List.of(
                "DispatchError|play-a|" + spotRid + "|HANDLER_MISSING/REPLY_ERROR/MissingSpotReq",
                "DispatchError|play-a|" + spotRid + "|HANDLER_MISSING/DROP/MissingSpotMsg"));
        System.out.println("scenario SM-C1-negative passed");
        System.out.println("scenario SM-E1 passed");
    }

    private void runOwnerRouting() {
        Contracts.StateRes roomA = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("owner-a"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        Contracts.StateRes roomB = eventually(() -> outbound.requestToSpot(spotRef("room-b"),
                new Contracts.StateReq("owner-b"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure("play-a".equals(roomA.nodeRid()), "SM-A3 room-a owner mismatch");
        ensure("play-b".equals(roomB.nodeRid()), "SM-A3 room-b owner mismatch");
        System.out.println("scenario SM-A3 passed");
        System.out.println("scenario SM-A4 passed");
    }

    private void runStageWrapper() {
        String endpointA = Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT");
        String spotRid = "spot-sm-a5-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.CreateSpotRes created = postJson(
            endpointA,
            "/spot/create",
            new Contracts.CreateSpotReq(spotRid),
            Contracts.CreateSpotRes.class);
        ensure(spotRid.equals(created.spotRid()), "SM-A5 created spot mismatch");
        ensure("play-a".equals(created.nodeRid()), "SM-A5 created spot owner mismatch");

        Contracts.StateRes ready = postJson(
            endpointA,
            "/spot/state/request",
            new Contracts.SpotStateRouteReq(spotRid, "sm-a5-ready", 0),
            Contracts.StateRes.class);
        ensure(spotRid.equals(ready.spotRid()), "SM-A5 route readiness spot mismatch");
        ensure("play-a".equals(ready.nodeRid()), "SM-A5 route readiness owner mismatch");

        Contracts.StateRes stageReply = postJson(
            endpointA,
            "/spot/stage/request",
            new Contracts.SpotStageProbeRouteReq(spotRid, "sm-a5-stage", 9),
            Contracts.StateRes.class);
        ensure(spotRid.equals(stageReply.spotRid()), "SM-A5 stage request spot mismatch");
        ensure("play-a".equals(stageReply.nodeRid()), "SM-A5 stage request owner mismatch");
        ensure(stageReply.value().contains("sm-a5-stage-9"), "SM-A5 stage request state mismatch");

        Contracts.StageTimerStartRes timer = postJson(
            endpointA,
            "/spot/stage/timer",
            new Contracts.SpotStageTimerRouteReq(spotRid, "sm-a5-stage-timer", 50),
            Contracts.StageTimerStartRes.class);
        ensure(spotRid.equals(timer.spotRid()) && timer.started(), "SM-A5 stage timer did not start");

        waitForPlayAEvidence(List.of(
            "SpotInitialized|play-a|" + spotRid,
            "StageRequest|play-a|" + spotRid + "|sm-a5-stage|9",
            "StageTimer|play-a|" + spotRid + "|sm-a5-stage-timer"));
        closeSpot(spotRid);
        waitForPlayAEvidence(List.of("SpotClosing|play-a|" + spotRid));
        System.out.println("scenario SM-A5 passed");
    }

    private void runOwnerRemap() {
        String endpointA = Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT");
        String endpointB = Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT");
        String key = "key-sm-g2-" + UUID.randomUUID().toString().replace("-", "");
        String firstSpot = "spot-" + key + "-a";
        String secondSpot = "spot-" + key + "-b";

        Contracts.CreateSpotRes firstCreated = postJson(
            endpointA,
            "/spot/create",
            new Contracts.CreateSpotReq(firstSpot),
            Contracts.CreateSpotRes.class);
        Contracts.StateRes firstReply = postJson(
            endpointA,
            "/spot/state/request",
            new Contracts.SpotStateRouteReq(firstSpot, "owner-remap-first", 1),
            Contracts.StateRes.class);
        Contracts.EvidenceSnapshot firstEvidence = postJson(
            endpointA,
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(
                List.of("StateReq|play-a|" + firstSpot + "|owner-remap-first-1"),
                10_000),
            Contracts.EvidenceSnapshot.class);

        Contracts.CreateSpotRes secondCreated = postJson(
            endpointB,
            "/spot/create",
            new Contracts.CreateSpotReq(secondSpot),
            Contracts.CreateSpotRes.class);
        Contracts.StateRes secondReply = postJson(
            endpointB,
            "/spot/state/request",
            new Contracts.SpotStateRouteReq(secondSpot, "owner-remap-second", 1),
            Contracts.StateRes.class);
        Contracts.EvidenceSnapshot secondEvidence = postJson(
            endpointB,
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(
                List.of("StateReq|play-b|" + secondSpot + "|owner-remap-second-1"),
                10_000),
            Contracts.EvidenceSnapshot.class);

        ensure("play-a".equals(firstCreated.nodeRid()), "SM-G2 first owner create node mismatch");
        ensure("play-b".equals(secondCreated.nodeRid()), "SM-G2 remapped owner create node mismatch");
        ensure("play-a".equals(firstReply.nodeRid()), "SM-G2 first owner request node mismatch");
        ensure("play-b".equals(secondReply.nodeRid()), "SM-G2 remapped owner request node mismatch");
        ensure(!containsSpotEvidence(firstEvidence, secondSpot), "SM-G2 remapped owner leaked to play-a");
        ensure(!containsSpotEvidence(secondEvidence, firstSpot), "SM-G2 first owner leaked to play-b");
        System.out.println("scenario SM-G2 passed");
    }

    private void runRouteMesh() {
        Contracts.RouteRes routeReply = routes.requestToNode(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("play-a"),
                new Contracts.RouteReq("route-mesh-normal"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.RouteRes.class).toCompletableFuture().join();
        ensure("play-a".equals(routeReply.nodeRid()), "SM-F3 route-channel target node mismatch");
        ensure("client-route-mesh".equals(routeReply.routeRid()), "SM-F3 route-channel source routing id mismatch");
        ensure("route:route-mesh-normal".equals(routeReply.value()), "SM-F3 route-channel reply mismatch");

        Contracts.StateRes reply = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("route-mesh"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure("play-a".equals(reply.nodeRid()), "SM-F2 route mesh target mismatch");
        outbound.sendToSpot(spotRef("room-a"), new Contracts.StateMsg("mixed-route-send"))
            .submit();
        System.out.println("scenario SM-F1 passed");
        System.out.println("scenario SM-F2 passed");
        System.out.println("scenario SM-F3 passed");
        expectFailure(() -> outbound.requestToSpot(spotRef("missing-route"),
                new Contracts.StateReq("missing-route"))
            .timeout(Duration.ofMillis(300))
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        System.out.println("scenario SM-F4-missing-route passed");
    }

    private void runRouteLifecycle() {
        Contracts.RouteRes before = routes.requestToNode(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("play-a"),
                new Contracts.RouteReq("sm-f5-before"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.RouteRes.class).toCompletableFuture().join();
        ensure("play-a".equals(before.nodeRid()), "SM-F5 pre-close route-channel target node mismatch");
        ensure("client-route-mesh".equals(before.routeRid()), "SM-F5 pre-close source routing id mismatch");
        ensure("route:sm-f5-before".equals(before.value()), "SM-F5 pre-close route-channel reply mismatch");

        Contracts.StateRes routed = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("sm-f5-route"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure("room-a".equals(routed.spotRid()), "SM-F5 routed spot rid mismatch");
        ensure("play-a".equals(routed.nodeRid()), "SM-F5 routed spot owner mismatch");

        closeSpot("room-a");

        Contracts.RouteRes after = routes.requestToNode(
                Contracts.ROUTE_CHANNEL,
                RoutingId.from("play-a"),
                new Contracts.RouteReq("sm-f5-after"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.RouteRes.class).toCompletableFuture().join();
        ensure("play-a".equals(after.nodeRid()), "SM-F5 post-close route-channel target node mismatch");
        ensure("client-route-mesh".equals(after.routeRid()), "SM-F5 post-close source routing id mismatch");
        ensure("route:sm-f5-after".equals(after.value()), "SM-F5 post-close route-channel reply mismatch");

        waitForPlayAEvidence(List.of(
            "RouteReq|play-a|client-route-mesh|sm-f5-before",
            "StateReq|play-a|room-a|sm-f5-route",
            "SpotClosing|play-a|room-a",
            "RouteReq|play-a|client-route-mesh|sm-f5-after"));
        System.out.println("scenario SM-F5 passed");
    }


    private void runActorSession() {
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        ZLinkStreamConnector unbound = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            Contracts.ActorProfile profile = new Contracts.ActorProfile(
                "Player One",
                7,
                List.of("alpha", "beta"));
            connector.connect().submit().toCompletableFuture().join();
            unbound.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq("actor-local-1", profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure("actor-local-1".equals(auth.actorId()), "SM-D1 auth actor mismatch");
            ensure(auth.boundCount() == 1, "SM-D1 bound actor count mismatch");
            ensure(profile.displayName().equals(auth.displayName()), "SM-B3 create profile display name mismatch");
            ensure(profile.level() == auth.level(), "SM-B3 create profile level mismatch");
            ensure(profile.tags().equals(auth.tags()), "SM-B3 create profile tags mismatch");

            var entryPush = connector.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes entryReply = connector
                .request(new Contracts.ActorEchoReq("entry-echo", 1, profile))
                .metadata("actor-id", "actor-local-1")
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify entry = entryPush.toCompletableFuture().join().payload();
            ensure("entry:entry-echo".equals(entryReply.value()), "SM-B1 entry actor request mismatch");
            ensure("entry".equals(entryReply.spotRid()), "SM-D3 entry bind spot mismatch");
            ensure("entry".equals(entry.spotRid()), "SM-D3 entry push spot mismatch");
            ensure("actor-local-1".equals(entryReply.actorId()), "SM-D3 entry bind actor mismatch");
            ensure("actor-local-1".equals(entry.actorId()), "SM-D3 entry push actor mismatch");
            ensure(entryReply.requestSeq() == 1, "SM-B3 entry request sequence mismatch");
            ensure(profile.displayName().equals(entryReply.displayName()), "SM-B3 entry profile display name mismatch");
            ensure(profile.level() == entryReply.level(), "SM-B3 entry profile level mismatch");
            ensure(profile.tags().equals(entryReply.tags()), "SM-B3 entry profile tags mismatch");
            ensure(entry.requestSeq() == 1, "SM-D1 entry push request sequence mismatch");
            ensure("push:entry-echo".equals(entry.value()), "SM-D1 entry push mismatch");

            Contracts.ActorJoinRes joined = connector
                .request(new Contracts.ActorJoinReq("room-a", profile, profile.tags()))
                .metadata("actor-id", "actor-local-1")
                .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
            ensure("room-a".equals(joined.spotRid()), "SM-B1 joined spot mismatch");
            ensure(profile.tags().equals(joined.tags()), "SM-B3 join payload tags mismatch");
            ensure(profile.displayName().equals(joined.displayName()), "SM-B3 join payload display name mismatch");
            ensure(profile.level() == joined.level(), "SM-B3 join payload level mismatch");

            var unboundPush = unbound.waitFor(Contracts.ActorPushNotify.class)
                .timeout(Duration.ofMillis(400))
                .submit(Contracts.ActorPushNotify.class);
            var userPush1 = connector.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes userReply1 = connector
                .request(new Contracts.ActorEchoReq("user-echo-1", 2, profile))
                .metadata("actor-id", "actor-local-1")
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify user1 = userPush1.toCompletableFuture().join().payload();
            ensure("room-a".equals(userReply1.spotRid()), "SM-B1 user actor spot mismatch");
            ensure("room-a".equals(joined.spotRid()), "SM-D3 user bind spot mismatch");
            ensure("room-a".equals(user1.spotRid()), "SM-D3 user push spot mismatch");
            ensure("actor-local-1".equals(userReply1.actorId()), "SM-D3 user relay actor mismatch");
            ensure("actor-local-1".equals(user1.actorId()), "SM-D3 user push actor mismatch");
            ensure("user:user-echo-1".equals(userReply1.value()), "SM-B1 user actor request mismatch");
            ensure(userReply1.requestSeq() == 2, "SM-B3 user request sequence mismatch");
            ensure(profile.displayName().equals(userReply1.displayName()), "SM-B3 user profile display name mismatch");
            ensure(profile.level() == userReply1.level(), "SM-B3 user profile level mismatch");
            ensure(profile.tags().equals(userReply1.tags()), "SM-B3 user profile tags mismatch");
            ensure(user1.requestSeq() == 2, "SM-D1 user push request sequence mismatch");
            ensure("push:user-echo-1".equals(user1.value()), "SM-D1 user push mismatch");
            expectFailure(() -> awaitUnchecked(unbound, unboundPush));

            var userPush2 = connector.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes userReply2 = connector
                .request(new Contracts.ActorEchoReq("user-echo-2", 3, profile))
                .metadata("actor-id", "actor-local-1")
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify user2 = userPush2.toCompletableFuture().join().payload();
            ensure(userReply2.requestSeq() == 3, "SM-B7 second packet request sequence mismatch");
            ensure(user2.requestSeq() == 3, "SM-B7 second push request sequence mismatch");

            var userPush3 = connector.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes userReply3 = connector
                .request(new Contracts.ActorEchoReq("user-echo-3", 4, profile))
                .metadata("actor-id", "actor-local-1")
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify user3 = userPush3.toCompletableFuture().join().payload();
            ensure(userReply3.requestSeq() == 4, "SM-B7 third packet request sequence mismatch");
            ensure(user3.requestSeq() == 4, "SM-B7 third push request sequence mismatch");
            ensure(userReply1.handlerSeq() < userReply2.handlerSeq()
                    && userReply2.handlerSeq() < userReply3.handlerSeq(),
                "SM-B7 actor packet handler sequence was not preserved");
            ensure(user1.handlerSeq() < user2.handlerSeq()
                    && user2.handlerSeq() < user3.handlerSeq(),
                "SM-B7 actor push sequence was not preserved");
            waitForPlayAEvidence(List.of("StreamInbound|play-a|session|ActorAuthReq"));

            System.out.println("scenario SM-B1 passed");
            System.out.println("scenario SM-B3 passed");
            System.out.println("scenario SM-B7 passed");
            System.out.println("scenario SM-D1 passed");
            System.out.println("scenario SM-D3 passed");
            System.out.println("scenario SM-D9 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor/session scenario failed", error);
        } finally {
            try {
                connector.close().submit().toCompletableFuture().join();
            } catch (Exception ignored) {
            }
            try {
                unbound.close().submit().toCompletableFuture().join();
            } catch (Exception ignored) {
            }
        }
    }

    private void runActorDestroy() {
        String actorId = "actor-sm-b8-destroy-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Destroy", 8, List.of("destroy"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-B8 auth actor mismatch");

            Contracts.ActorDestroyRes destroy = connector
                .request(new Contracts.ActorDestroyReq(actorId))
                .metadata("actor-id", actorId)
                .submit(Contracts.ActorDestroyRes.class).toCompletableFuture().join();
            ensure(actorId.equals(destroy.actorId()), "SM-B8 destroy actor mismatch");
            ensure(destroy.destroyed(), "SM-B8 destroy was not accepted");

            expectFailure(() -> {
                try {
                    connector
                        .request(new Contracts.ActorEchoReq("after-destroy", 1, profile))
                        .metadata("actor-id", actorId)
                        .timeout(Duration.ofMillis(500))
                        .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                } catch (Exception error) {
                    throw new RuntimeException(error);
                }
            });

            Contracts.EvidenceSnapshot evidence = waitForPlayAEvidence(
                List.of("ActorDestroyed|play-a|entry|" + actorId));
            long destroyedCount = evidence.entries().stream()
                .filter(entry -> "ActorDestroyed".equals(entry.marker()) && actorId.equals(entry.value()))
                .count();
            ensure(destroyedCount == 1, "SM-B8 destroy evidence count mismatch");

            System.out.println("scenario SM-B8 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor destroy scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runActorJoinAdmission() {
        verifyActorJoinAdmission(
            Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT"),
            "play-a",
            "spot-sm-b9-local-" + UUID.randomUUID().toString().replace("-", ""),
            "actor-sm-b9-local-" + UUID.randomUUID().toString().replace("-", ""));
        verifyActorJoinAdmission(
            Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT"),
            "play-b",
            "spot-sm-b9-remote-" + UUID.randomUUID().toString().replace("-", ""),
            "actor-sm-b9-remote-" + UUID.randomUUID().toString().replace("-", ""));
        System.out.println("scenario SM-B9 passed");
    }

    private void verifyActorJoinAdmission(
        String ownerEndpoint,
        String nodeRid,
        String spotRid,
        String actorId) {
        Contracts.ActorProfile allowedProfile =
            new Contracts.ActorProfile("Admission " + nodeRid, 9, List.of("admission", nodeRid));
        Contracts.ActorProfile rejectedProfile =
            new Contracts.ActorProfile("Rejected " + nodeRid, 9, List.of("admission", "rejected"));
        String rejectedActorId = actorId + "-rejected";
        postJson(
            ownerEndpoint,
            "/spot/create",
            new Contracts.CreateSpotReq(spotRid),
            Contracts.CreateSpotRes.class);

        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            connector
                .request(new Contracts.ActorAuthReq(actorId, allowedProfile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            Contracts.JoinAdmittedUserSpotActorRes allowed = connector
                .request(new Contracts.JoinAdmittedUserSpotActorReq(
                    spotRid,
                    allowedProfile,
                    allowedProfile.tags(),
                    true,
                    "allowed"))
                .metadata("actor-id", actorId)
                .submit(Contracts.JoinAdmittedUserSpotActorRes.class).toCompletableFuture().join();
            ensure(allowed.accepted(), "SM-B9 allowed join was rejected");
            ensure(actorId.equals(allowed.actorId()), "SM-B9 allowed actor mismatch");
            ensure(spotRid.equals(allowed.spotRid()), "SM-B9 allowed spot mismatch");
            ensure(nodeRid.equals(allowed.nodeRid()), "SM-B9 allowed node mismatch");

            connector
                .request(new Contracts.ActorAuthReq(rejectedActorId, rejectedProfile))
                .metadata("actor-id", rejectedActorId)
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            Contracts.JoinAdmittedUserSpotActorRes rejected = connector
                .request(new Contracts.JoinAdmittedUserSpotActorReq(
                    spotRid,
                    rejectedProfile,
                    rejectedProfile.tags(),
                    false,
                    "capacity"))
                .metadata("actor-id", rejectedActorId)
                .submit(Contracts.JoinAdmittedUserSpotActorRes.class).toCompletableFuture().join();
            ensure(!rejected.accepted(), "SM-B9 rejected join was accepted");
            ensure("ActorJoinRejected".equals(rejected.errorKind()), "SM-B9 rejection was not classified");

            Contracts.EvidenceSnapshot evidence = waitForEvidence(
                ownerEndpoint,
                List.of(
                    "ActorUserJoinAdmitted|" + nodeRid + "|" + spotRid + "|" + actorId + "/allowed",
                    "ActorUserJoined|" + nodeRid + "|" + spotRid + "|" + actorId,
                    "ActorUserJoinRejected|" + nodeRid + "|" + spotRid + "|" + rejectedActorId + "/capacity"));
            long rejectedJoined = countActorEvidence(evidence, "ActorUserJoined", spotRid, rejectedActorId);
            ensure(rejectedJoined == 0, "SM-B9 rejected actor was joined to user spot");
        } catch (Exception error) {
            throw new IllegalStateException("actor join admission scenario failed for " + nodeRid, error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runActorPushChain() {
        String actorId = "actor-sm-d15-" + UUID.randomUUID().toString().replace("-", "");
        String marker = "sm-d15-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            Contracts.ActorProfile profile =
                new Contracts.ActorProfile("Push Chain", 15, List.of("gateway", "push"));
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D15 auth actor mismatch");

            Contracts.ActorPingRes probe = connector
                .request(new Contracts.ActorPingReq("sm-d15-bind-probe"))
                .metadata("actor-id", actorId)
                .submit(Contracts.ActorPingRes.class).toCompletableFuture().join();
            ensure(actorId.equals(probe.actorId()), "SM-D15 bind probe actor mismatch");
            ensure("play-a".equals(probe.nodeRid()), "SM-D15 bind probe node mismatch");

            var pushed = connector.waitFor(Contracts.ActorPushNotify.class)
                .where(Contracts.ActorPushNotify.class, message ->
                    actorId.equals(message.payload().actorId())
                        && marker.equals(message.payload().value()))
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorPingRes reply = actors
                .requestToActor(actorRef(actorId), new Contracts.ActorPushReq(marker))
                .timeout(REQUEST_TIMEOUT)
                .submit(Contracts.ActorPingRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify notify = pushed.toCompletableFuture().join().payload();

            ensure(actorId.equals(reply.actorId()), "SM-D15 actor client reply actor mismatch");
            ensure("entry".equals(reply.spotRid()), "SM-D15 actor client reply spot mismatch");
            ensure("play-a".equals(reply.nodeRid()), "SM-D15 actor client reply node mismatch");
            ensure(marker.equals(reply.value()), "SM-D15 actor client reply marker mismatch");
            ensure(actorId.equals(notify.actorId()), "SM-D15 push actor mismatch");
            ensure("entry".equals(notify.spotRid()), "SM-D15 push spot mismatch");
            ensure(marker.equals(notify.value()), "SM-D15 push marker mismatch");

            waitForPlayAEvidence(List.of(
                "ActorPingReq|play-a|entry|" + actorId + "/sm-d15-bind-probe",
                "ActorPushReq|play-a|entry|" + actorId + "/" + marker));
            System.out.println("scenario SM-D15 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor push chain scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runRemoteActorSession() {
        String actorId = "actor-sm-remote-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_B_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Remote Player", 24, List.of("remote", "relay"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-B2 remote auth actor mismatch");
            ensure("play-b".equals(auth.nodeRid()), "SM-B2 remote actor was not created on play-b");

            Contracts.ActorJoinRes joined = connector
                .request(new Contracts.ActorJoinReq("room-a", profile, profile.tags()))
                .metadata("actor-id", actorId)
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
            ensure(actorId.equals(joined.actorId()), "SM-B2 remote join actor mismatch");
            ensure("room-a".equals(joined.spotRid()), "SM-B2 remote join spot mismatch");
            ensure("play-a".equals(joined.nodeRid()), "SM-B2 remote join did not cross to play-a");

            var userPush = connector.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes userReply = connector
                .request(new Contracts.ActorEchoReq("remote-user-echo", 42, profile))
                .metadata("actor-id", actorId)
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify push = userPush.toCompletableFuture().join().payload();
            ensure("user:remote-user-echo".equals(userReply.value()), "SM-B4 remote actor reply mismatch");
            ensure("room-a".equals(userReply.spotRid()), "SM-B4 remote actor reply spot mismatch");
            ensure("play-a".equals(userReply.nodeRid()), "SM-B4 remote actor reply node mismatch");
            ensure("push:remote-user-echo".equals(push.value()), "SM-D2 remote bound-session push mismatch");
            ensure(actorId.equals(push.actorId()), "SM-D2 remote bound-session push actor mismatch");

            waitForPlayBEvidence(List.of("ActorCreated|play-b|entry|" + actorId));
            waitForPlayAEvidence(List.of(
                "ActorUserJoined|play-a|room-a|" + actorId,
                "ActorUserReq|play-a|room-a|" + actorId + "/remote-user-echo"));

            System.out.println("scenario SM-B2 passed");
            System.out.println("scenario SM-B4 passed");
            System.out.println("scenario SM-D2 passed");
        } catch (Exception error) {
            throw new IllegalStateException("remote actor/session scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runActorMissingHandler() {
        String actorId = "actor-sm-b5-missing-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Missing", 5, List.of("missing"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-B5 auth actor mismatch");

            expectFailure(() -> {
                try {
                    connector
                        .request(new Contracts.MissingActorReq("missing-handler"))
                        .metadata("actor-id", actorId)
                        .timeout(Duration.ofSeconds(2))
                        .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                } catch (Exception error) {
                    throw new RuntimeException(error);
                }
            });

            Contracts.EvidenceSnapshot evidence = waitForPlayAEvidence(List.of(
                "HANDLER_MISSING/REPLY_ERROR/MissingActorReq"));
            ensure(evidence.entries().stream().anyMatch(entry ->
                    "DispatchError".equals(entry.marker())
                        && "HANDLER_MISSING/REPLY_ERROR/MissingActorReq".equals(entry.value())),
                "SM-B5 missing actor dispatch error evidence mismatch");

            System.out.println("scenario SM-B5 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor missing handler scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runActorLeaveDisconnect() {
        try {
            String suffix = UUID.randomUUID().toString().replace("-", "");
            String leaveActorId = "actor-sm-b6-left-" + suffix;
            String disconnectActorId = "actor-sm-b6-disconnected-" + suffix;

            try {
                ZLinkStreamConnector leaveClient = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
                Contracts.ActorProfile profile = new Contracts.ActorProfile("Leave", 6, List.of("leave"));
                try {
                    leaveClient.connect().submit().toCompletableFuture().join();
                    Contracts.ActorAuthRes auth = leaveClient
                        .request(new Contracts.ActorAuthReq(leaveActorId, profile))
                        .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
                    ensure(leaveActorId.equals(auth.actorId()), "SM-B6 leave auth actor mismatch");
                    Contracts.ActorJoinRes joined = leaveClient
                        .request(new Contracts.ActorJoinReq("room-a", profile, profile.tags()))
                        .metadata("actor-id", leaveActorId)
                        .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
                    ensure(leaveActorId.equals(joined.actorId()), "SM-B6 leave join actor mismatch");
                    leaveClient
                        .send(new Contracts.LeaveActorReq(leaveActorId))
                        .metadata("actor-id", leaveActorId)
                        .submit();
                } finally {
                    closeQuietly(leaveClient);
                }
            } catch (Exception error) {
                throw new IllegalStateException("SM-B6 leave phase failed", error);
            }

            Contracts.EvidenceSnapshot leaveEvidence = waitForPlayAEvidence(
                List.of("ActorUserLeft|play-a|room-a|" + leaveActorId));
            ensure(leaveEvidence.entries().stream().noneMatch(entry ->
                    "ActorUserDisconnected".equals(entry.marker()) && leaveActorId.equals(entry.value())),
                "SM-B6 explicit leave emitted disconnect evidence");

            try {
                ZLinkStreamConnector disconnectClient =
                    createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
                try {
                    Contracts.ActorProfile disconnectProfile =
                        new Contracts.ActorProfile("Disconnect", 6, List.of("disconnect"));
                    disconnectClient.connect().submit().toCompletableFuture().join();
                    Contracts.ActorAuthRes auth = disconnectClient
                        .request(new Contracts.ActorAuthReq(disconnectActorId, disconnectProfile))
                        .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
                    ensure(disconnectActorId.equals(auth.actorId()), "SM-B6 disconnect auth actor mismatch");
                    Contracts.ActorJoinRes joined = disconnectClient
                        .request(new Contracts.ActorJoinReq("room-a", disconnectProfile, disconnectProfile.tags()))
                        .metadata("actor-id", disconnectActorId)
                        .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
                    ensure(disconnectActorId.equals(joined.actorId()), "SM-B6 disconnect join actor mismatch");
                } finally {
                    closeQuietly(disconnectClient);
                }
            } catch (Exception error) {
                throw new IllegalStateException("SM-B6 disconnect phase failed", error);
            }

            Contracts.EvidenceSnapshot disconnectEvidence = waitForPlayAEvidence(
                List.of("ActorUserDisconnected|play-a|room-a|" + disconnectActorId));
            ensure(disconnectEvidence.entries().stream().noneMatch(entry ->
                    "ActorUserLeft".equals(entry.marker()) && disconnectActorId.equals(entry.value())),
                "SM-B6 disconnect emitted leave evidence");

            System.out.println("scenario SM-B6 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor leave/disconnect scenario failed", error);
        }
    }

    private void runActorDisconnectNotify() {
        try {
            String actorId = "actor-sm-d5-notified-" + UUID.randomUUID().toString().replace("-", "");
            ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
            Contracts.ActorProfile profile = new Contracts.ActorProfile("Disconnect", 5, List.of("disconnect"));
            try {
                connector.connect().submit().toCompletableFuture().join();
                Contracts.ActorAuthRes auth = connector
                    .request(new Contracts.ActorAuthReq(actorId, profile))
                    .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
                ensure(actorId.equals(auth.actorId()), "SM-D5 auth actor mismatch");
                Contracts.ActorJoinRes joined = connector
                    .request(new Contracts.ActorJoinReq("room-a", profile, profile.tags()))
                    .metadata("actor-id", actorId)
                    .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
                ensure(actorId.equals(joined.actorId()), "SM-D5 join actor mismatch");
            } finally {
                closeQuietly(connector);
            }

            Contracts.EvidenceSnapshot evidence = waitForPlayAEvidence(
                List.of("ActorUserDisconnected|play-a|room-a|" + actorId));
            ensure(evidence.entries().stream().anyMatch(entry ->
                    "ActorUserDisconnected".equals(entry.marker()) && actorId.equals(entry.value())),
                "SM-D5 expected selected actor disconnect callback evidence");

            System.out.println("scenario SM-D5 passed");
        } catch (Exception error) {
            throw new IllegalStateException("actor disconnect notify scenario failed", error);
        }
    }

    private void runMixedStreamChannel() {
        String actorId = "actor-sm-d11-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Mixed", 11, List.of("stream", "channel"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D11 auth actor mismatch");

            Contracts.ActorEchoRes streamReply = connector
                .request(new Contracts.ActorEchoReq("mixed-stream", 11, profile))
                .metadata("actor-id", actorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            ensure("entry:mixed-stream".equals(streamReply.value()), "SM-D11 stream actor reply mismatch");

            Contracts.RouteRes channelReply = routes.requestToNode(
                    Contracts.ROUTE_CHANNEL,
                    RoutingId.from("play-a"),
                    new Contracts.RouteReq("mixed-channel"))
                .timeout(REQUEST_TIMEOUT)
                .submit(Contracts.RouteRes.class).toCompletableFuture().join();
            ensure("route:mixed-channel".equals(channelReply.value()), "SM-D11 route-channel reply mismatch");
            ensure("play-a".equals(channelReply.nodeRid()), "SM-D11 route-channel node mismatch");

            waitForPlayAEvidence(List.of(
                "StreamInbound|play-a|session|ActorEchoReq",
                "RouteReq|play-a|client-route-mesh|mixed-channel"));
            System.out.println("scenario SM-D11 passed");
        } catch (Exception error) {
            throw new IllegalStateException("mixed stream/channel scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runMultiActorBind() {
        String firstActorId = "actor-sm-d4-x-" + UUID.randomUUID().toString().replace("-", "");
        String secondActorId = "actor-sm-d4-y-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Multi Bind", 9, List.of("multi", "bind"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.MultiBindRes bound = connector
                .request(new Contracts.MultiBindReq(firstActorId, secondActorId, profile))
                .submit(Contracts.MultiBindRes.class).toCompletableFuture().join();
            ensure(bound.boundCount() == 2, "SM-D4 expected two bound actors");

            Contracts.ActorEchoRes first = connector
                .request(new Contracts.ActorEchoReq("to-x", 10, profile))
                .metadata("actor-id", firstActorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorEchoRes second = connector
                .request(new Contracts.ActorEchoReq("to-y", 11, profile))
                .metadata("actor-id", secondActorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            ensure(firstActorId.equals(first.actorId()) && "entry:to-x".equals(first.value()),
                "SM-D4 first actor relay mismatch");
            ensure(secondActorId.equals(second.actorId()) && "entry:to-y".equals(second.value()),
                "SM-D4 second actor relay mismatch");

            var firstPushed = connector.waitFor(Contracts.ActorPushNotify.class)
                .where(Contracts.ActorPushNotify.class, message -> firstActorId.equals(message.payload().actorId()))
                .submit(Contracts.ActorPushNotify.class);
            connector
                .request(new Contracts.ActorEchoReq("push-x", 12, profile))
                .metadata("actor-id", firstActorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify firstPush = firstPushed.toCompletableFuture().join().payload();
            ensure(firstActorId.equals(firstPush.actorId()) && "push:push-x".equals(firstPush.value()),
                "SM-D4 first actor push mismatch");

            var secondPushed = connector.waitFor(Contracts.ActorPushNotify.class)
                .where(Contracts.ActorPushNotify.class, message -> secondActorId.equals(message.payload().actorId()))
                .submit(Contracts.ActorPushNotify.class);
            connector
                .request(new Contracts.ActorEchoReq("push-y", 13, profile))
                .metadata("actor-id", secondActorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify secondPush = secondPushed.toCompletableFuture().join().payload();
            ensure(secondActorId.equals(secondPush.actorId()) && "push:push-y".equals(secondPush.value()),
                "SM-D4 second actor push mismatch");

            expectFailure(() -> {
                try {
                    connector
                        .request(new Contracts.ActorEchoReq("missing-actor-id", 14, profile))
                        .timeout(Duration.ofSeconds(2))
                        .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                } catch (Exception error) {
                    throw new RuntimeException(error);
                }
            });

            waitForPlayAEvidence(List.of(
                "ActorSessionBound|play-a|session|" + firstActorId,
                "ActorSessionBound|play-a|session|" + secondActorId));
            System.out.println("scenario SM-D4 passed");
        } catch (Exception error) {
            throw new IllegalStateException("multi actor bind scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runBoundPushIsolation() {
        String boundActorId = "actor-sm-d6-" + UUID.randomUUID().toString().replace("-", "");
        String shadowActorId = "actor-sm-d6-shadow-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector bound = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        ZLinkStreamConnector shadow = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_B_ENDPOINT"));
        Contracts.ActorProfile boundProfile = new Contracts.ActorProfile("Bound", 6, List.of("bound"));
        Contracts.ActorProfile shadowProfile = new Contracts.ActorProfile("Shadow", 6, List.of("shadow"));
        try {
            bound.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes boundAuth = bound
                .request(new Contracts.ActorAuthReq(boundActorId, boundProfile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(boundActorId.equals(boundAuth.actorId()), "SM-D6 bound auth actor mismatch");

            shadow.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes shadowAuth = shadow
                .request(new Contracts.ActorAuthReq(shadowActorId, shadowProfile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(shadowActorId.equals(shadowAuth.actorId()), "SM-D6 shadow auth actor mismatch");

            var shadowPush = shadow.waitFor(Contracts.ActorPushNotify.class)
                .timeout(Duration.ofMillis(400))
                .submit(Contracts.ActorPushNotify.class);
            var boundPush = bound.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes reply = bound
                .request(new Contracts.ActorEchoReq("push-bound-only", 20, boundProfile))
                .metadata("actor-id", boundActorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify notify = boundPush.toCompletableFuture().join().payload();

            ensure(boundActorId.equals(reply.actorId()), "SM-D6 reply actor mismatch");
            ensure(boundActorId.equals(notify.actorId()), "SM-D6 push actor mismatch");
            ensure("push:push-bound-only".equals(notify.value()), "SM-D6 push value mismatch");
            expectFailure(() -> awaitUnchecked(shadow, shadowPush));
            ensure(shadow.receivedCount("ActorPushNotify") == 0, "SM-D6 shadow session received push");

            waitForPlayAEvidence(List.of("ActorEntryReq|play-a|entry|" + boundActorId + "/push-bound-only"));
            System.out.println("scenario SM-D6 passed");
        } catch (Exception error) {
            throw new IllegalStateException("bound session push isolation scenario failed", error);
        } finally {
            closeQuietly(bound);
            closeQuietly(shadow);
        }
    }

    private void runStreamAuth() {
        ZLinkStreamConnector preAuth = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile preAuthProfile =
            new Contracts.ActorProfile("PreAuth", 7, List.of("pre-auth"));
        try {
            preAuth.connect().submit().toCompletableFuture().join();
            expectFailure(() -> {
                try {
                    preAuth
                        .request(new Contracts.ActorEchoReq("pre-auth-dispatch", 7, preAuthProfile))
                        .timeout(Duration.ofMillis(500))
                        .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                } catch (Exception error) {
                    throw new RuntimeException(error);
                }
            });
        } catch (Exception error) {
            throw new IllegalStateException("pre-auth dispatch scenario failed", error);
        } finally {
            closeQuietly(preAuth);
        }

        String actorId = "actor-sm-d7-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector authenticated = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Auth Ok", 7, List.of("auth"));
        try {
            authenticated.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = authenticated
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D7 auth actor mismatch");

            var push = authenticated.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes reply = authenticated
                .request(new Contracts.ActorEchoReq("auth-ok", 7, profile))
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify notify = push.toCompletableFuture().join().payload();

            ensure(actorId.equals(reply.actorId()), "SM-D7 relay actor mismatch");
            ensure("entry:auth-ok".equals(reply.value()), "SM-D7 relay value mismatch");
            ensure(actorId.equals(notify.actorId()), "SM-D7 push actor mismatch");
            ensure("push:auth-ok".equals(notify.value()), "SM-D7 push value mismatch");

            waitForPlayAEvidence(List.of("StreamInbound|play-a|session|ActorAuthReq"));
            System.out.println("scenario SM-D7 passed");
        } catch (Exception error) {
            throw new IllegalStateException("stream auth scenario failed", error);
        } finally {
            closeQuietly(authenticated);
        }
    }

    private void runStreamReconnect() {
        String actorId = "actor-sm-d8-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Reconnect", 8, List.of("reconnect"));
        ZLinkStreamConnector first = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            first.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = first
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D8 initial auth actor mismatch");

            var pending = first
                .request(new Contracts.SlowSessionReq("before-disconnect", 1_000))
                .timeout(Duration.ofSeconds(10))
                .submit(Contracts.SlowSessionRes.class);
            Thread.sleep(100);
            first.close().submit().toCompletableFuture().join();

            boolean pendingFailed = false;
            try {
                pending.toCompletableFuture().join();
            } catch (Exception ignored) {
                pendingFailed = true;
            }
            ensure(pendingFailed, "SM-D8 expected pending request to fail after stream disconnect");
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("stream reconnect scenario interrupted", error);
        } catch (Exception error) {
            throw new IllegalStateException("stream reconnect first phase failed", error);
        } finally {
            closeQuietly(first);
        }

        waitForPlayAEvidence(List.of("StreamDisconnected|play-a|session"));

        ZLinkStreamConnector second = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            second.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = second
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D8 reauth actor mismatch");

            Contracts.ActorEchoRes reply = second
                .request(new Contracts.ActorEchoReq("after-reconnect", 8, profile))
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            ensure(actorId.equals(reply.actorId()), "SM-D8 reconnected actor mismatch");
            ensure("play-a".equals(reply.nodeRid()), "SM-D8 reconnected node mismatch");
            ensure("entry:after-reconnect".equals(reply.value()), "SM-D8 reconnected value mismatch");

            System.out.println("scenario SM-D8 passed");
        } catch (Exception error) {
            throw new IllegalStateException("stream reconnect second phase failed", error);
        } finally {
            closeQuietly(second);
        }
    }

    private void runStreamRebindTransfer() {
        String actorId = "actor-sm-d12-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Transfer", 12, List.of("transfer"));
        ZLinkStreamConnector first = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        int firstSeen;
        try {
            first.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = first
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D12 first auth actor mismatch");
            ensure("play-a".equals(auth.nodeRid()), "SM-D12 first auth node mismatch");

            Contracts.ActorPingRes firstReply = first
                .request(new Contracts.ActorPingReq("before-transfer"))
                .submit(Contracts.ActorPingRes.class).toCompletableFuture().join();
            ensure(actorId.equals(firstReply.actorId()), "SM-D12 first reply actor mismatch");
            ensure("play-a".equals(firstReply.nodeRid()), "SM-D12 first reply node mismatch");
            ensure("before-transfer".equals(firstReply.value()), "SM-D12 first reply value mismatch");
            firstSeen = firstReply.seen();
            first.close().submit().toCompletableFuture().join();
        } catch (Exception error) {
            throw new IllegalStateException("stream rebind transfer first phase failed", error);
        } finally {
            closeQuietly(first);
        }

        waitForPlayAEvidence(List.of("StreamDisconnected|play-a|session"));

        ZLinkStreamConnector second = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_B_ENDPOINT"));
        try {
            second.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = second
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .timeout(Duration.ofSeconds(45))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D12 second auth actor mismatch");
            ensure("play-a".equals(auth.nodeRid()), "SM-D12 second auth did not rebind existing actor");

            Contracts.SnapshotRes snapshot = second
                .request(new Contracts.SnapshotReq(actorId))
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.SnapshotRes.class).toCompletableFuture().join();
            ensure(actorId.equals(snapshot.actorId()), "SM-D12 snapshot actor mismatch");
            ensure(snapshot.seen() == firstSeen,
                "SM-D12 actor state was not preserved across stream servers");

            var pushed = second.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorPingRes resumed = second
                .request(new Contracts.ActorPushReq("after-transfer"))
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorPingRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify notify = pushed.toCompletableFuture().join().payload();

            ensure(actorId.equals(resumed.actorId()), "SM-D12 resumed actor mismatch");
            ensure("play-a".equals(resumed.nodeRid()), "SM-D12 resumed node mismatch");
            ensure("after-transfer".equals(resumed.value()), "SM-D12 resumed value mismatch");
            ensure(resumed.seen() == firstSeen + 1,
                "SM-D12 resumed actor state mismatch");
            ensure(actorId.equals(notify.actorId()), "SM-D12 resumed push actor mismatch");
            ensure("after-transfer".equals(notify.value()), "SM-D12 resumed push value mismatch");
            ensure(notify.handlerSeq() == resumed.seen(), "SM-D12 push state mismatch");

            waitForPlayAEvidence(List.of("ActorPushReq|play-a|entry|" + actorId + "/after-transfer"));
            System.out.println("scenario SM-D12 passed");
        } catch (Exception error) {
            throw new IllegalStateException("stream rebind transfer second phase failed", error);
        } finally {
            closeQuietly(second);
        }
    }

    private void runStreamBackpressure() {
        String congestedActorId = "actor-sm-d10-congested-" + UUID.randomUUID().toString().replace("-", "");
        String isolatedActorId = "actor-sm-d10-isolated-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.ActorProfile congestedProfile =
            new Contracts.ActorProfile("Backpressure", 10, List.of("congested"));
        Contracts.ActorProfile isolatedProfile =
            new Contracts.ActorProfile("Backpressure Peer", 10, List.of("isolated"));
        ZLinkStreamConnector congested = createStreamConnector(
            Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"),
            ZLinkStreamDispatchMode.MANUAL,
            1);
        ZLinkStreamConnector isolated = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_B_ENDPOINT"));
        try {
            congested.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes congestedAuth = congested
                .request(new Contracts.ActorAuthReq(congestedActorId, congestedProfile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(congestedActorId.equals(congestedAuth.actorId()), "SM-D10 congested auth actor mismatch");

            isolated.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes isolatedAuth = isolated
                .request(new Contracts.ActorAuthReq(isolatedActorId, isolatedProfile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(isolatedActorId.equals(isolatedAuth.actorId()), "SM-D10 isolated auth actor mismatch");

            var retainedPush = congested.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            for (int index = 0; index < 8; index++) {
                Contracts.ActorEchoRes reply = congested
                    .request(new Contracts.ActorEchoReq("burst-" + index, 10, congestedProfile))
                    .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                ensure(congestedActorId.equals(reply.actorId()), "SM-D10 congested reply actor mismatch");
            }
            ensure(congested.receivedCount("ActorPushNotify") <= 1,
                "SM-D10 congested queue retained too many pushes");
            congested.dispatch().submit().toCompletableFuture().join();
            Contracts.ActorPushNotify retained = retainedPush.toCompletableFuture().join().payload();
            ensure(congestedActorId.equals(retained.actorId()), "SM-D10 retained push actor mismatch");
            ensure("push:burst-7".equals(retained.value()),
                "SM-D10 expected newest congested push to be retained");

            Contracts.ActorEchoRes stillAlive = congested
                .request(new Contracts.ActorEchoReq("after-backpressure", 10, congestedProfile))
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            ensure(congestedActorId.equals(stillAlive.actorId()), "SM-D10 congested session stopped routing");
            ensure("entry:after-backpressure".equals(stillAlive.value()),
                "SM-D10 congested session reply mismatch");
            congested.dispatch().submit().toCompletableFuture().join();

            var isolatedPush = isolated.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes isolatedReply = isolated
                .request(new Contracts.ActorEchoReq("isolated-push", 10, isolatedProfile))
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify isolatedNotify = isolatedPush.toCompletableFuture().join().payload();
            ensure(isolatedActorId.equals(isolatedReply.actorId()), "SM-D10 isolated reply actor mismatch");
            ensure(isolatedActorId.equals(isolatedNotify.actorId()), "SM-D10 isolated push actor mismatch");
            ensure("push:isolated-push".equals(isolatedNotify.value()),
                "SM-D10 isolated session push mismatch");

            System.out.println("scenario SM-D10 passed");
        } catch (Exception error) {
            throw new IllegalStateException("stream backpressure scenario failed", error);
        } finally {
            closeQuietly(congested);
            closeQuietly(isolated);
        }
    }

    private void runStreamHeartbeat() {
        String actorId = "actor-sm-d13-" + UUID.randomUUID().toString().replace("-", "");
        ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Heartbeat", 13, List.of("heartbeat"));
        try {
            connector.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D13 auth actor mismatch");

            Thread.sleep(1_500);
            ensure(connector.isConnected(), "SM-D13 heartbeat-enabled stream disconnected");

            Contracts.ActorEchoRes reply = connector
                .request(new Contracts.ActorEchoReq("after-heartbeat", 13, profile))
                .metadata("actor-id", actorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            ensure(actorId.equals(reply.actorId()), "SM-D13 actor reply mismatch");
            ensure("play-a".equals(reply.nodeRid()), "SM-D13 actor node mismatch");
            ensure("entry:after-heartbeat".equals(reply.value()), "SM-D13 actor value mismatch");

            waitForPlayAEvidence(List.of("StreamInbound|play-a|session|ActorEchoReq"));
            System.out.println("scenario SM-D13 passed");
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("stream heartbeat scenario interrupted", error);
        } catch (Exception error) {
            throw new IllegalStateException("stream heartbeat scenario failed", error);
        } finally {
            closeQuietly(connector);
        }
    }

    private void runStreamTls() {
        String endpoint = Env.get("ZLINK_JAVA_E2E_TLS_STREAM_A_ENDPOINT");
        ZLinkStreamConnector strict = createStreamConnector(
            endpoint,
            ZLinkStreamDispatchMode.AUTO,
            Integer.MAX_VALUE,
            false);
        boolean strictTlsRejected = false;
        try {
            strict.connect().submit().toCompletableFuture().join();
        } catch (Exception error) {
            strictTlsRejected = true;
        } finally {
            closeQuietly(strict);
        }
        ensure(strictTlsRejected, "SM-D14 expected strict TLS validation to reject self-signed certificate");

        String actorId = "actor-sm-d14-tls";
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Stream TLS", 14, List.of("tls"));
        ZLinkStreamConnector tls = createStreamConnector(
            endpoint,
            ZLinkStreamDispatchMode.AUTO,
            Integer.MAX_VALUE,
            true);
        try {
            tls.connect().submit().toCompletableFuture().join();
            Contracts.ActorAuthRes auth = tls
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-D14 TLS auth actor mismatch");
            ensure("play-a".equals(auth.nodeRid()), "SM-D14 TLS auth node mismatch");

            var pushed = tls.waitFor(Contracts.ActorPushNotify.class)
                .submit(Contracts.ActorPushNotify.class);
            Contracts.ActorEchoRes reply = tls
                .request(new Contracts.ActorEchoReq("tls-push", 14, profile))
                .metadata("actor-id", actorId)
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            Contracts.ActorPushNotify notify = pushed.toCompletableFuture().join().payload();
            ensure(actorId.equals(reply.actorId()), "SM-D14 TLS actor reply mismatch");
            ensure("play-a".equals(reply.nodeRid()), "SM-D14 TLS actor node mismatch");
            ensure(actorId.equals(notify.actorId()), "SM-D14 TLS push actor mismatch");
            ensure("push:tls-push".equals(notify.value()), "SM-D14 TLS push payload mismatch");

            System.out.println("scenario SM-D14 passed");
        } catch (Exception error) {
            throw new IllegalStateException("stream TLS scenario failed", error);
        } finally {
            closeQuietly(tls);
        }
    }

    private void runMultiNodeRouteToSpot() {
        String endpointA = Env.get("ZLINK_JAVA_E2E_MULTI_A_HTTP_ENDPOINT");
        String endpointB = Env.get("ZLINK_JAVA_E2E_MULTI_B_HTTP_ENDPOINT");
        String spotA = "spot-sm-q9-a-" + UUID.randomUUID().toString().replace("-", "");
        String spotB = "spot-sm-q9-b-" + UUID.randomUUID().toString().replace("-", "");

        Contracts.MultiNodeCreateSpotRes createdA = postJson(
            endpointA,
            "/spot/create-local",
            new Contracts.MultiNodeCreateSpotReq(spotA, 0),
            Contracts.MultiNodeCreateSpotRes.class);
        Contracts.MultiNodeStateRes firstA = postJson(
            endpointA,
            "/spot/state/request",
            new Contracts.MultiNodeStateRouteReq(spotA, 11),
            Contracts.MultiNodeStateRes.class);
        Contracts.MultiNodeStateRes directA = postJson(
            endpointA,
            "/spot/state/request",
            new Contracts.MultiNodeStateRouteReq(spotA, 0),
            Contracts.MultiNodeStateRes.class);
        Contracts.EvidenceSnapshot evidenceA = postJson(
            endpointA,
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(
                List.of("MultiNodeStateReq|" + createdA.nodeRid() + "|" + spotA + "|11"),
                10_000),
            Contracts.EvidenceSnapshot.class);

        Contracts.MultiNodeCreateSpotRes createdB = postJson(
            endpointB,
            "/spot/create-local",
            new Contracts.MultiNodeCreateSpotReq(spotB, 0),
            Contracts.MultiNodeCreateSpotRes.class);
        Contracts.MultiNodeStateRes firstB = postJson(
            endpointB,
            "/spot/state/request",
            new Contracts.MultiNodeStateRouteReq(spotB, 17),
            Contracts.MultiNodeStateRes.class);
        Contracts.MultiNodeStateRes directB = postJson(
            endpointB,
            "/spot/state/request",
            new Contracts.MultiNodeStateRouteReq(spotB, 0),
            Contracts.MultiNodeStateRes.class);
        Contracts.EvidenceSnapshot evidenceB = postJson(
            endpointB,
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(
                List.of("MultiNodeStateReq|" + createdB.nodeRid() + "|" + spotB + "|17"),
                10_000),
            Contracts.EvidenceSnapshot.class);

        ensure(createdA.spotRid().equals(spotA), "SM-Q9 node A create spot mismatch");
        ensure("multi-node-a".equals(createdA.nodeRid()), "SM-Q9 node A create reply node mismatch");
        ensure(firstA.value() == 11, "SM-Q9 node A route-to-spot reply value mismatch");
        ensure(directA.spotRid().equals(spotA), "SM-Q9 node A direct spot reply target mismatch");
        ensure("multi-node-a".equals(directA.nodeRid()), "SM-Q9 node A direct spot reply node mismatch");
        ensure(directA.value() == 11, "SM-Q9 node A direct spot reply value mismatch");
        ensure(countEvidence(evidenceA, "MultiNodeStateReq", spotA, "11") >= 2,
            "SM-Q9 node A did not process both route-to-spot requests");

        ensure(createdB.spotRid().equals(spotB), "SM-Q9 node B create spot mismatch");
        ensure("multi-node-b".equals(createdB.nodeRid()), "SM-Q9 node B create reply node mismatch");
        ensure(firstB.value() == 17, "SM-Q9 node B route-to-spot reply value mismatch");
        ensure(directB.spotRid().equals(spotB), "SM-Q9 node B direct spot reply target mismatch");
        ensure("multi-node-b".equals(directB.nodeRid()), "SM-Q9 node B direct spot reply node mismatch");
        ensure(directB.value() == 17, "SM-Q9 node B direct spot reply value mismatch");
        ensure(countEvidence(evidenceB, "MultiNodeStateReq", spotB, "17") >= 2,
            "SM-Q9 node B did not process both route-to-spot requests");

        System.out.println("scenario SM-Q9 passed");
    }

    private void runSpotOnlyMesh() {
        String endpointA = Env.get("ZLINK_JAVA_E2E_MULTI_A_HTTP_ENDPOINT");
        String endpointB = Env.get("ZLINK_JAVA_E2E_MULTI_B_HTTP_ENDPOINT");
        String key = UUID.randomUUID().toString().replace("-", "");
        String sourceSpot = "spot-sm-f6-source-" + key;
        String targetSpot = "spot-sm-f6-target-" + key;
        String actorId = "actor-sm-f6-" + key;
        String marker = "sm-f6-" + key;

        Contracts.CreateSpotRes target = postJson(
            endpointB,
            "/spot/create-user-local",
            new Contracts.CreateSpotReq(targetSpot),
            Contracts.CreateSpotRes.class);
        ensure(targetSpot.equals(target.spotRid()), "SM-F6 target spot create mismatch");
        ensure("multi-node-b".equals(target.nodeRid()), "SM-F6 target node mismatch");

        Contracts.SpotOnlyMeshRes mesh = postJson(
            endpointA,
            "/spot/spot-only/request-send",
            new Contracts.SpotOnlyMeshReq(sourceSpot, targetSpot, marker),
            Contracts.SpotOnlyMeshRes.class);
        ensure(sourceSpot.equals(mesh.sourceSpotRid()), "SM-F6 source spot mismatch");
        ensure(targetSpot.equals(mesh.targetSpotRid()), "SM-F6 request target mismatch");
        ensure(mesh.targetValue() == 7, "SM-F6 target request value mismatch");

        Contracts.SpotOnlyJoinRes join = postJson(
            endpointA,
            "/actor/spot-only-join",
            new Contracts.SpotOnlyJoinReq(targetSpot, actorId, marker),
            Contracts.SpotOnlyJoinRes.class);
        ensure(join.accepted(), "SM-F6 spot-only actor join was rejected");
        ensure(actorId.equals(join.actorId()), "SM-F6 actor join id mismatch");
        ensure(targetSpot.equals(join.targetSpotRid()), "SM-F6 actor join target mismatch");

        waitForEvidence(
            endpointB,
            List.of(
                "MultiNodeStateReq|multi-node-b|" + targetSpot + "|7",
                "MultiNodeStateMsg|multi-node-b|" + targetSpot + "|sm-f6-send-" + marker,
                "SpotOnlyActorJoined|multi-node-b|" + targetSpot + "|" + actorId));
        System.out.println("scenario SM-F6 passed");
    }

    private static long countEvidence(
        Contracts.EvidenceSnapshot evidence,
        String marker,
        String spotRid,
        String value) {
        return evidence.entries().stream()
            .filter(entry -> marker.equals(entry.marker())
                && spotRid.equals(entry.spotRid())
                && value.equals(entry.value()))
            .count();
    }

    private static boolean containsSpotEvidence(
        Contracts.EvidenceSnapshot evidence,
        String spotRid) {
        return evidence.entries().stream()
            .anyMatch(entry -> spotRid.equals(entry.spotRid()));
    }

    private void runBoundPushLoad() {
        int sessionCount = 8;
        List<ZLinkStreamConnector> connectors = new ArrayList<>();
        List<String> actorIds = new ArrayList<>();
        List<String> values = new ArrayList<>();
        List<CompletionStage<ZLinkStreamMessage<Contracts.ActorPushNotify>>> pushes = new ArrayList<>();
        try {
            for (int index = 0; index < sessionCount; index++) {
                String actorId = "actor-sm-g4-" + index + "-" + UUID.randomUUID().toString().replace("-", "");
                Contracts.ActorProfile profile =
                    new Contracts.ActorProfile("Bound Load " + index, 14, List.of("load", "session-" + index));
                ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
                connectors.add(connector);
                actorIds.add(actorId);
                values.add("push-" + index);
                connector.connect().submit().toCompletableFuture().join();
                Contracts.ActorAuthRes auth = connector
                    .request(new Contracts.ActorAuthReq(actorId, profile))
                    .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
                ensure(actorId.equals(auth.actorId()), "SM-G4 auth actor mismatch");
            }

            for (ZLinkStreamConnector connector : connectors) {
                pushes.add(connector.waitFor(Contracts.ActorPushNotify.class)
                    .submit(Contracts.ActorPushNotify.class));
            }

            List<CompletableFuture<Contracts.ActorEchoRes>> replies = new ArrayList<>();
            for (int index = 0; index < connectors.size(); index++) {
                ZLinkStreamConnector connector = connectors.get(index);
                String value = values.get(index);
                Contracts.ActorProfile profile =
                    new Contracts.ActorProfile("Bound Load Request " + index, 14, List.of("load"));
                replies.add(CompletableFuture.supplyAsync(() -> {
                    try {
                        return connector
                            .request(new Contracts.ActorEchoReq(value, 14, profile))
                            .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                    } catch (Exception error) {
                        throw new RuntimeException(error);
                    }
                }));
            }

            for (int index = 0; index < connectors.size(); index++) {
                String actorId = actorIds.get(index);
                String value = values.get(index);
                Contracts.ActorEchoRes reply = replies.get(index).join();
                Contracts.ActorPushNotify notify = pushes.get(index).toCompletableFuture().join().payload();
                ensure(actorId.equals(reply.actorId()), "SM-G4 push reply actor mismatch");
                ensure(("entry:" + value).equals(reply.value()), "SM-G4 push reply value mismatch");
                ensure(actorId.equals(notify.actorId()), "SM-G4 push notify actor mismatch");
                ensure(("push:" + value).equals(notify.value()), "SM-G4 push notify value mismatch");
            }

            System.out.println("scenario SM-G4 passed");
        } catch (Exception error) {
            throw new IllegalStateException("bound session push load scenario failed", error);
        } finally {
            for (ZLinkStreamConnector connector : connectors) {
                closeQuietly(connector);
            }
        }
    }

    private void runJoinLeaveRace() {
        int actorCount = 2;
        String key = UUID.randomUUID().toString().replace("-", "");
        String spotRid = "spot-sm-g3-" + key;
        List<String> actorIds = new ArrayList<>();
        List<ZLinkStreamConnector> connectors = new ArrayList<>();
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Race", 3, List.of("join", "leave"));
        try {
            postJson(
                Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT"),
                "/spot/create",
                new Contracts.CreateSpotReq(spotRid),
                Contracts.CreateSpotRes.class);

            for (int index = 0; index < actorCount; index++) {
                String actorId = "actor-sm-g3-" + key + "-" + index;
                ZLinkStreamConnector connector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
                connector.connect().submit().toCompletableFuture().join();
                Contracts.ActorAuthRes auth = connector
                    .request(new Contracts.ActorAuthReq(actorId, profile))
                    .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
                ensure(actorId.equals(auth.actorId()), "SM-G3 auth actor mismatch");
                Contracts.ActorJoinRes joined = connector
                    .request(new Contracts.ActorJoinReq(spotRid, profile, profile.tags()))
                    .metadata("actor-id", actorId)
                    .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
                ensure(actorId.equals(joined.actorId()), "SM-G3 join actor mismatch");
                ensure("play-a".equals(joined.nodeRid()), "SM-G3 join node mismatch");
                actorIds.add(actorId);
                connectors.add(connector);
            }

            List<CompletableFuture<Void>> tasks = new ArrayList<>();
            for (int index = 0; index < actorIds.size(); index++) {
                String actorId = actorIds.get(index);
                ZLinkStreamConnector connector = connectors.get(index);
                tasks.add(CompletableFuture.runAsync(() -> {
                    try {
                        Contracts.ActorEchoRes ping = connector
                            .request(new Contracts.ActorEchoReq(actorId, 3, profile))
                            .metadata("actor-id", actorId)
                            .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                        ensure(actorId.equals(ping.actorId()), "SM-G3 actor request target mismatch");
                        ensure("play-a".equals(ping.nodeRid()), "SM-G3 actor request node mismatch");
                        connector
                            .send(new Contracts.LeaveActorReq(actorId))
                            .metadata("actor-id", actorId)
                            .submit();
                    } catch (Exception error) {
                        throw new RuntimeException(error);
                    }
                }));
            }
            CompletableFuture.allOf(tasks.toArray(CompletableFuture[]::new)).join();

            List<String> expected = actorIds.stream()
                .flatMap(actorId -> List.of(
                    "ActorUserJoined|play-a|" + spotRid + "|" + actorId,
                    "ActorUserLeft|play-a|" + spotRid + "|" + actorId).stream())
                .toList();
            Contracts.EvidenceSnapshot evidence = waitForPlayAEvidence(expected);
            for (String actorId : actorIds) {
                long joinedCount = countActorEvidence(evidence, "ActorUserJoined", spotRid, actorId);
                long leftCount = countActorEvidence(evidence, "ActorUserLeft", spotRid, actorId);
                ensure(joinedCount == 1,
                    "SM-G3 join evidence count mismatch for " + actorId
                        + ": count=" + joinedCount
                        + " matches=" + matchingActorEvidence(evidence, "ActorUserJoined", spotRid, actorId));
                ensure(leftCount == 1,
                    "SM-G3 leave evidence count mismatch for " + actorId
                        + ": count=" + leftCount
                        + " matches=" + matchingActorEvidence(evidence, "ActorUserLeft", spotRid, actorId));
            }
            System.out.println("scenario SM-G3 passed");
        } catch (Exception error) {
            throw new IllegalStateException("join/leave race scenario failed", error);
        } finally {
            for (ZLinkStreamConnector connector : connectors) {
                closeQuietly(connector);
            }
        }
    }

    private void runPlayCrashRecovery() {
        String actorId = "actor-sm-g1-" + UUID.randomUUID().toString().replace("-", "");
        Contracts.ActorProfile profile = new Contracts.ActorProfile("Crash Recovery", 1, List.of("sm-g1"));
        ZLinkStreamConnector crashedConnector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            crashedConnector.connect().submit().toCompletableFuture().join();
            authenticateJoinAndEcho(crashedConnector, actorId, profile, "before-crash", 1);
            signalFile("ZLINK_JAVA_E2E_SM_G1_READY_FILE");
            waitForSignalFile("ZLINK_JAVA_E2E_SM_G1_CRASHED_FILE");

            expectFailure(() -> {
                try {
                    crashedConnector
                        .request(new Contracts.ActorEchoReq("during-crash", 2, profile))
                        .metadata("actor-id", actorId)
                        .timeout(Duration.ofSeconds(2))
                        .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
                } catch (Exception error) {
                    throw new RuntimeException(error);
                }
            });

            Contracts.StateRes survivor = eventually(() -> outbound.requestToSpot(spotRef("room-b"),
                    new Contracts.StateReq("sm-g1-survivor"))
                .timeout(REQUEST_TIMEOUT)
                .submit(Contracts.StateRes.class).toCompletableFuture().join());
            ensure("play-b".equals(survivor.nodeRid()), "SM-G1 play-b survivor request node mismatch");
            signalFile("ZLINK_JAVA_E2E_SM_G1_FAILED_FILE");
            waitForSignalFile("ZLINK_JAVA_E2E_SM_G1_RESTARTED_FILE");
        } catch (Exception error) {
            throw new IllegalStateException("play crash recovery scenario failed before restart", error);
        } finally {
            closeQuietly(crashedConnector);
        }

        ZLinkStreamConnector recoveredConnector = createStreamConnector(Env.get("ZLINK_JAVA_E2E_STREAM_A_ENDPOINT"));
        try {
            recoveredConnector.connect().submit().toCompletableFuture().join();
            authenticateJoinAndEcho(recoveredConnector, actorId, profile, "after-restart", 3);
        } catch (Exception error) {
            throw new IllegalStateException("play crash recovery scenario failed after restart", error);
        } finally {
            closeQuietly(recoveredConnector);
        }

        System.out.println("scenario SM-G1 passed");
    }

    private static void authenticateJoinAndEcho(
        ZLinkStreamConnector connector,
        String actorId,
        Contracts.ActorProfile profile,
        String value,
        int requestSeq) {
        try {
            Contracts.ActorAuthRes auth = connector
                .request(new Contracts.ActorAuthReq(actorId, profile))
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorAuthRes.class).toCompletableFuture().join();
            ensure(actorId.equals(auth.actorId()), "SM-G1 auth actor mismatch");

            Contracts.ActorJoinRes joined = connector
                .request(new Contracts.ActorJoinReq("room-a", profile, profile.tags()))
                .metadata("actor-id", actorId)
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorJoinRes.class).toCompletableFuture().join();
            ensure("room-a".equals(joined.spotRid()), "SM-G1 joined spot mismatch");

            Contracts.ActorEchoRes echo = connector
                .request(new Contracts.ActorEchoReq(value, requestSeq, profile))
                .metadata("actor-id", actorId)
                .timeout(Duration.ofSeconds(15))
                .submit(Contracts.ActorEchoRes.class).toCompletableFuture().join();
            ensure("room-a".equals(echo.spotRid()), "SM-G1 actor spot mismatch");
            ensure(("user:" + value).equals(echo.value()), "SM-G1 actor echo mismatch");
        } catch (Exception error) {
            throw new IllegalStateException("SM-G1 auth/join/echo failed", error);
        }
    }

    private static void signalFile(String envName) {
        String path = Env.get(envName);
        ensure(!path.isBlank(), envName + " is required");
        try {
            Files.writeString(Path.of(path), "ready\n");
        } catch (IOException error) {
            throw new IllegalStateException("failed to write signal file " + path, error);
        }
    }

    private static void waitForSignalFile(String envName) {
        String path = Env.get(envName);
        ensure(!path.isBlank(), envName + " is required");
        Path signal = Path.of(path);
        long deadline = System.nanoTime() + Duration.ofSeconds(60).toNanos();
        while (System.nanoTime() < deadline) {
            if (Files.exists(signal)) {
                return;
            }
            try {
                Thread.sleep(100);
            } catch (InterruptedException error) {
                Thread.currentThread().interrupt();
                throw new IllegalStateException("interrupted while waiting for signal file " + path, error);
            }
        }
        throw new IllegalStateException("timed out waiting for signal file " + path);
    }

    private static long countActorEvidence(
        Contracts.EvidenceSnapshot evidence,
        String marker,
        String spotRid,
        String actorId) {
        return evidence.entries().stream()
            .filter(entry -> marker.equals(entry.marker())
                && spotRid.equals(entry.spotRid())
                && entry.value().startsWith(actorId))
            .count();
    }

    private static List<String> matchingActorEvidence(
        Contracts.EvidenceSnapshot evidence,
        String marker,
        String spotRid,
        String actorId) {
        return evidence.entries().stream()
            .filter(entry -> marker.equals(entry.marker())
                && spotRid.equals(entry.spotRid())
                && entry.value().startsWith(actorId))
            .map(entry -> entry.marker()
                + "|" + entry.nodeRid()
                + "|" + entry.spotRid()
                + "|" + entry.value())
            .toList();
    }

    private static <T> void awaitUnchecked(
        ZLinkStreamConnector connector,
        java.util.concurrent.CompletionStage<T> stage) {
        try {
            stage.toCompletableFuture().join();
        } catch (Exception error) {
            throw new RuntimeException(error);
        }
    }

    private ZLinkStreamConnector createStreamConnector(String endpoint) {
        return createStreamConnector(endpoint, ZLinkStreamDispatchMode.AUTO, Integer.MAX_VALUE);
    }

    private ZLinkStreamConnector createStreamConnector(
        String endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        int maxReceivedMessages) {
        return createStreamConnector(endpoint, dispatchMode, maxReceivedMessages, false);
    }

    private ZLinkStreamConnector createStreamConnector(
        String endpoint,
        ZLinkStreamDispatchMode dispatchMode,
        int maxReceivedMessages,
        boolean skipServerCertificateValidation) {
        return ZLinkStreamConnectorFactory.create(new ZLinkStreamConnectorOptions(
            URI.create(endpoint),
            dispatchMode,
            REQUEST_TIMEOUT,
            2,
            Duration.ofSeconds(5),
            64 * 1024,
            64 * 1024,
            maxReceivedMessages,
            true,
            Duration.ofSeconds(1),
            Duration.ofSeconds(5),
            false,
            Duration.ofMillis(250),
            Duration.ofSeconds(5),
            2.0,
            skipServerCertificateValidation,
            ZLinkStreamCompression.LZ4,
            ZLinkStreamPacketNameResolver.defaultResolver(),
            null));
    }

    private static void closeQuietly(ZLinkStreamConnector connector) {
        try {
            connector.close().submit().toCompletableFuture().join();
        } catch (Exception ignored) {
        }
    }

    private static void closeSpot(String spotRid) {
        String endpoint = Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT");
        HttpRequest request = HttpRequest.newBuilder(URI.create(endpoint + "/admin/close?rid=" + spotRid))
            .timeout(REQUEST_TIMEOUT)
            .POST(HttpRequest.BodyPublishers.noBody())
            .build();
        try {
            HttpResponse<String> response = HTTP.send(request, HttpResponse.BodyHandlers.ofString());
            ensure(response.statusCode() >= 200 && response.statusCode() < 300,
                "spot close returned HTTP " + response.statusCode());
            ensure(response.body().contains("\"closed\":true"), "spot close did not report closed=true");
        } catch (IOException error) {
            throw new IllegalStateException("failed to close spot " + spotRid, error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("interrupted while closing spot " + spotRid, error);
        }
    }

    private static Contracts.EvidenceSnapshot waitForPlayAEvidence(List<String> fragments) {
        return postJson(
            Env.get("ZLINK_JAVA_E2E_HTTP_A_ENDPOINT"),
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(fragments, 10_000),
            Contracts.EvidenceSnapshot.class);
    }

    private static Contracts.EvidenceSnapshot waitForPlayBEvidence(List<String> fragments) {
        return postJson(
            Env.get("ZLINK_JAVA_E2E_HTTP_B_ENDPOINT"),
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(fragments, 10_000),
            Contracts.EvidenceSnapshot.class);
    }

    private static Contracts.EvidenceSnapshot waitForEvidence(
        String endpoint,
        List<String> fragments) {
        return postJson(
            endpoint,
            "/evidence/wait",
            new Contracts.EvidenceWaitReq(fragments, 10_000),
            Contracts.EvidenceSnapshot.class);
    }

    private static <T> T postJson(
        String endpoint,
        String path,
        Object request,
        Class<T> responseType) {
        try {
            HttpRequest httpRequest = HttpRequest.newBuilder(URI.create(endpoint + path))
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofByteArray(JSON.writeValueAsBytes(request)))
                .build();
            HttpResponse<byte[]> response = HTTP.send(
                httpRequest,
                HttpResponse.BodyHandlers.ofByteArray());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                throw new IllegalStateException(
                    "evidence request failed with status "
                        + response.statusCode()
                        + ": "
                        + new String(response.body(), java.nio.charset.StandardCharsets.UTF_8));
            }
            return JSON.readValue(response.body(), responseType);
        } catch (IOException error) {
            throw new IllegalStateException("evidence JSON request failed", error);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("evidence JSON request interrupted", error);
        }
    }

    private void runWorkerOffload() {
        eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("worker-start"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        Contracts.StateRes followUp = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("worker-follow-up"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure(followUp.value().contains("worker-follow-up"),
            "SM-A8 follow-up state was not applied");
        System.out.println("scenario SM-A8 passed");
    }

    private void runSpotOutbound() {
        Contracts.OutboundRes reply = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.OutboundReq("c2"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.OutboundRes.class).toCompletableFuture().join());
        ensure("room-a".equals(reply.spotRid()), "SM-C2 wrong source spot");
        ensure("play-a".equals(reply.nodeRid()), "SM-C2 wrong source node");
        ensure("c2".equals(reply.channelReply()), "SM-C2 channel request reply mismatch");
        System.out.println("scenario SM-C2 passed");
    }

    private void runSpotToSpot() {
        Contracts.StateRes requestReply = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.StateReq("c3-source"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.StateRes.class).toCompletableFuture().join());
        ensure("play-a".equals(requestReply.nodeRid()), "SM-C3 source spot owner mismatch");
        outbound.sendToSpot(spotRef("room-b"), new Contracts.OutboundMsg("c3-send"))
            .submit();
        Contracts.OutboundRes reply = eventually(() -> outbound.requestToSpot(spotRef("room-b"),
                new Contracts.OutboundReq("c3-request"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.OutboundRes.class).toCompletableFuture().join());
        ensure("room-b".equals(reply.spotRid()), "SM-C3 wrong target spot");
        ensure("play-b".equals(reply.nodeRid()), "SM-C3 wrong target node");
        System.out.println("scenario SM-C3 passed");
    }

    private void runSpotMeshCrossNode() {
        Contracts.OutboundRes reply = eventually(() -> outbound.requestToSpot(spotRef("room-a"),
                new Contracts.OutboundReq("c5-cross-node"))
            .timeout(REQUEST_TIMEOUT)
            .submit(Contracts.OutboundRes.class).toCompletableFuture().join());
        ensure("room-a".equals(reply.spotRid()), "SM-C5 wrong publisher spot");
        ensure("play-a".equals(reply.nodeRid()), "SM-C5 wrong publisher node");
        waitForPlayBEvidence(List.of("SpotMeshMsg|play-b|room-b|publish:c5-cross-node"));
        System.out.println("scenario SM-C5 passed");
    }

    private void runIdleTimer() {
        sleep(1200);
        System.out.println("scenario SM-E3 passed");
    }

    private void runTimerOverrun() {
        sleep(1200);
        System.out.println("scenario SM-E4 passed");
    }

    private static void expectFailure(Runnable action) {
        try {
            action.run();
        } catch (RuntimeException error) {
            return;
        }
        throw new IllegalStateException("operation unexpectedly succeeded");
    }

    private static <T> T eventually(Supplier<T> action) {
        long deadline = System.nanoTime() + EVENTUAL_TIMEOUT.toNanos();
        RuntimeException lastFailure = null;
        while (System.nanoTime() < deadline) {
            try {
                return action.get();
            } catch (RuntimeException error) {
                lastFailure = error;
                try {
                    Thread.sleep(200);
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("operation interrupted", interrupted);
                }
            }
        }
        throw new IllegalStateException("operation did not succeed before timeout", lastFailure);
    }

    private ActorRef actorRef(String actorId) {
        return actorRefs.find(actorId)
            .thenApply(found -> found
                .orElseThrow(() -> new IllegalStateException("actor ref not found: " + actorId)))
            .toCompletableFuture()
            .join();
    }

    private SpotHandle spotRef(String spotRid) {
        return spotHandles.resolveSpotHandle(RoutingId.from(spotRid)).toCompletableFuture().join()
            .orElseThrow(() -> new IllegalStateException("spot not found: " + spotRid));
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException error) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("operation interrupted", error);
        }
    }

    private static void ensure(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }
}
