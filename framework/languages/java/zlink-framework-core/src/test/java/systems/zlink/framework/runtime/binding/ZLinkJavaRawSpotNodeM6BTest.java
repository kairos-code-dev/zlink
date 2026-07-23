package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorLifecycleEventKind;
import systems.zlink.framework.runtime.backend.ZLinkBackendRecvMode;
import systems.zlink.framework.runtime.backend.ZLinkBackendRequestResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotDispatchEvent;

final class ZLinkJavaRawSpotNodeM6BTest {
    @Test
    void spotSendRejectsStaleGenerationBeforeDispatch() {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-node");
            RoutingId sourceRid = RoutingId.from("jvm-m6b-source");
            RoutingId targetRid = RoutingId.from("jvm-m6b-target");
            node.setRoutingId(nodeRid);
            ZLinkBackendSpot source = node.spotNode().createSpot(sourceRid);
            ZLinkBackendSpot target = node.spotNode().createSpot(targetRid);

            try (Message stale = Message.from("stale")) {
                assertFalse(source.sendToSpot(
                    nodeRid,
                    targetRid,
                    target.lifecycleGeneration() + 1,
                    List.of(stale),
                    SendFlags.DONT_WAIT));
            }
            try (Message current = Message.from("current")) {
                assertTrue(source.sendToSpot(
                    nodeRid,
                    targetRid,
                    target.lifecycleGeneration(),
                    List.of(current),
                    SendFlags.DONT_WAIT));
            }
            try (var received = target.recvRoute(ZLinkBackendRecvMode.DONT_WAIT)) {
                assertNotNull(received);
                assertEquals(
                    "current",
                    received.parts().getFirst().toUtf8String());
            }
        }
    }

    @Test
    void spotRequestPublishesOnlyTheFirstTerminalReply() throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-request-node");
            RoutingId sourceRid = RoutingId.from("jvm-m6b-request-source");
            RoutingId targetRid = RoutingId.from("jvm-m6b-request-target");
            node.setRoutingId(nodeRid);
            ZLinkBackendSpot source = node.spotNode().createSpot(sourceRid);
            ZLinkBackendSpot target = node.spotNode().createSpot(targetRid);
            AtomicInteger callbackCount = new AtomicInteger();
            CompletableFuture<String> reply = new CompletableFuture<>();
            target.onDispatchEvent(info -> {
                if (info.event() != ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try (var received =
                         target.recvRoute(ZLinkBackendRecvMode.DONT_WAIT);
                     Message first = Message.from("first");
                     Message late = Message.from("late")) {
                    received.reply(List.of(first));
                    received.reply(List.of(late));
                }
            });

            try (Message request = Message.from("request")) {
                assertTrue(source.requestToSpot(
                    nodeRid,
                    targetRid,
                    target.lifecycleGeneration(),
                    List.of(request),
                    received -> {
                        callbackCount.incrementAndGet();
                        try (received) {
                            reply.complete(
                                received.parts().getFirst().toUtf8String());
                        }
                    },
                    SendFlags.DONT_WAIT,
                    Duration.ofSeconds(1)));
            }

            assertEquals("first", reply.get(1, TimeUnit.SECONDS));
            Thread.sleep(20);
            assertEquals(1, callbackCount.get());
        }
    }

    @Test
    void actorJoinCommitsMembershipBeforeLeaveLifecycle() throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-join-node");
            RoutingId targetRid = RoutingId.from("jvm-m6b-join-target");
            node.setRoutingId(nodeRid);
            ZLinkBackendSpot target = node.spotNode().createSpot(targetRid);
            target.onDispatchEvent(info -> {
                if (info.event()
                    != ZLinkBackendSpotDispatchEvent.ACTOR_JOIN_READABLE) {
                    return;
                }
                var request =
                    target.recvActorJoin(ZLinkBackendRecvMode.DONT_WAIT);
                target.replyActorJoin(request, 0, List.of());
            });

            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor;
            try (Message create = Message.from("create")) {
                actor = node.spotNode().createActor("actor-1", create);
            }
            var joined = node.spotNode().joinActor(
                actor,
                nodeRid,
                targetRid,
                target.lifecycleGeneration(),
                List.of(),
                Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);

            assertEquals(ZLinkBackendRequestResult.OK, joined.result());
            assertEquals(targetRid, joined.joinedSpotRid());
            assertEquals(2, joined.joinEpoch());

            node.spotNode().leaveActor(
                actor, targetRid, Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            var lifecycle =
                target.recvActorLifecycle(ZLinkBackendRecvMode.DONT_WAIT);
            assertNotNull(lifecycle);
            assertEquals(
                ZLinkBackendActorLifecycleEventKind.LEFT,
                lifecycle.kind());
            assertEquals(3, lifecycle.info().joinEpoch());
        }
    }

    @Test
    void unansweredSpotRequestTerminatesAtDeadline() throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-timeout-node");
            RoutingId sourceRid = RoutingId.from("jvm-m6b-timeout-source");
            RoutingId targetRid = RoutingId.from("jvm-m6b-timeout-target");
            node.setRoutingId(nodeRid);
            ZLinkBackendSpot source = node.spotNode().createSpot(sourceRid);
            ZLinkBackendSpot target = node.spotNode().createSpot(targetRid);
            CompletableFuture<ZLinkBackendRequestResult> result =
                new CompletableFuture<>();

            try (Message request = Message.from("request")) {
                assertTrue(source.requestToSpot(
                    nodeRid,
                    targetRid,
                    target.lifecycleGeneration(),
                    List.of(request),
                    received -> {
                        try (received) {
                            result.complete(received.result());
                        }
                    },
                    SendFlags.DONT_WAIT,
                    Duration.ofMillis(20)));
            }

            assertEquals(
                ZLinkBackendRequestResult.TIMED_OUT,
                result.get(1, TimeUnit.SECONDS));
            try (var queued =
                     target.recvRoute(ZLinkBackendRecvMode.DONT_WAIT)) {
                assertNotNull(queued);
            }
        }
    }

    @Test
    void remoteSpotSendAndRequestUseTheExactRouteFence() throws Exception {
        String endpoint = "inproc://jvm-m6b-remote-" + System.nanoTime();
        RoutingId leftRid = RoutingId.from("jvm-m6b-remote-left");
        RoutingId rightRid = RoutingId.from("jvm-m6b-remote-right");
        RoutingId targetRid = RoutingId.from("jvm-m6b-remote-target");
        try (var context = Zlink.createContext();
             var left = new ZLinkJavaRawMeshNode(context, "mesh");
             var right = new ZLinkJavaRawMeshNode(context, "mesh")) {
            left.setRoutingId(leftRid);
            left.setBind(endpoint);
            right.setRoutingId(rightRid);
            right.setBind(
                "inproc://jvm-m6b-remote-right-" + System.nanoTime());
            left.start();
            right.start();
            right.connectPeer(endpoint, leftRid);
            ZLinkBackendSpot source = right.spotNode().createSpot(
                RoutingId.from("jvm-m6b-remote-source"));
            ZLinkBackendSpot target = left.spotNode().createSpot(targetRid);
            CompletableFuture<String> sent = new CompletableFuture<>();
            target.onDispatchEvent(info -> {
                if (info.event() != ZLinkBackendSpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try (var received =
                         target.recvRoute(ZLinkBackendRecvMode.DONT_WAIT)) {
                    String value =
                        received.parts().getLast().toUtf8String();
                    if (received.requestSeq().isPresent()) {
                        try (Message reply = Message.from("remote-reply")) {
                            received.reply(List.of(reply));
                        }
                    } else {
                        sent.complete(value);
                    }
                }
            });

            long deadline =
                System.nanoTime() + Duration.ofSeconds(2).toNanos();
            boolean submitted = false;
            while (!submitted && System.nanoTime() < deadline) {
                try (Message message = Message.from("remote-send")) {
                    submitted = source.sendToSpot(
                        leftRid,
                        targetRid,
                        target.lifecycleGeneration(),
                        List.of(message),
                        SendFlags.DONT_WAIT);
                }
                if (!submitted) {
                    Thread.sleep(1);
                }
            }
            assertTrue(submitted);
            assertEquals("remote-send", sent.get(2, TimeUnit.SECONDS));

            CompletableFuture<String> reply = new CompletableFuture<>();
            try (Message message = Message.from("remote-request")) {
                assertTrue(source.requestToSpot(
                    leftRid,
                    targetRid,
                    target.lifecycleGeneration(),
                    List.of(message),
                    received -> {
                        try (received) {
                            reply.complete(
                                received.parts().getFirst().toUtf8String());
                        }
                    },
                    SendFlags.DONT_WAIT,
                    Duration.ofSeconds(2)));
            }
            assertEquals(
                "remote-reply",
                reply.get(2, TimeUnit.SECONDS));
        }
    }

    @Test
    void localActorRequestRunsOnItsOwningSpotAndRepliesOnce()
        throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-actor-node");
            node.setRoutingId(nodeRid);
            ZLinkBackendSpot entry = node.spotNode().entrySpot();
            entry.onDispatchEvent(info -> {
                if (info.event()
                    != ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                    return;
                }
                var received = info.actorMessages().getFirst();
                try (Message reply = Message.from("actor-reply")) {
                    node.spotNode().replyActorNoBind(
                        received.actor(),
                        received.sourceNodeRid(),
                        received.sourceSessionRid(),
                        received.requestId(),
                        received.flags(),
                        List.of(reply));
                } finally {
                    info.actorMessages().forEach(
                        systems.zlink.framework.runtime.backend
                            .ZLinkBackendActorReceived::close);
                }
            });
            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor;
            try (Message create = Message.from("create")) {
                actor = node.spotNode().createActor("actor-local", create);
            }

            List<Message> reply;
            try (Message request = Message.from("actor-request")) {
                reply = node.spotNode().requestToActor(
                    actor,
                    List.of(request),
                    SendFlags.DONT_WAIT,
                    Duration.ofSeconds(1)).toCompletableFuture()
                    .get(1, TimeUnit.SECONDS);
            }
            try {
                assertEquals(
                    "actor-reply",
                    reply.getFirst().toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
            assertFalse(node.spotNode().sendToActor(
                new systems.zlink.framework.runtime.backend
                    .ZLinkBackendActorRef(
                        nodeRid,
                        actor.actorId(),
                        actor.generation() + 1),
                List.of(),
                SendFlags.DONT_WAIT));
        }
    }

    @Test
    void remoteActorRequestRunsOnTheCurrentOwningSpot() throws Exception {
        String endpoint = "inproc://jvm-m6b-actor-remote-"
            + System.nanoTime();
        RoutingId leftRid = RoutingId.from("jvm-m6b-actor-left");
        RoutingId rightRid = RoutingId.from("jvm-m6b-actor-right");
        try (var context = Zlink.createContext();
             var left = new ZLinkJavaRawMeshNode(context, "mesh");
             var right = new ZLinkJavaRawMeshNode(context, "mesh")) {
            left.setRoutingId(leftRid);
            left.setBind(endpoint);
            right.setRoutingId(rightRid);
            right.setBind(
                "inproc://jvm-m6b-actor-right-" + System.nanoTime());
            left.start();
            right.start();
            right.connectPeer(endpoint, leftRid);
            ZLinkBackendSpot entry = left.spotNode().entrySpot();
            entry.onDispatchEvent(info -> {
                if (info.event()
                    != ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                    return;
                }
                var received = info.actorMessages().getFirst();
                try (Message reply = Message.from("remote-actor-reply")) {
                    left.spotNode().replyActorNoBind(
                        received.actor(),
                        received.sourceNodeRid(),
                        received.sourceSessionRid(),
                        received.requestId(),
                        received.flags(),
                        List.of(reply));
                } finally {
                    info.actorMessages().forEach(
                        systems.zlink.framework.runtime.backend
                            .ZLinkBackendActorReceived::close);
                }
            });
            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor;
            try (Message create = Message.from("create")) {
                actor = left.spotNode().createActor("actor-remote", create);
            }

            long deadline =
                System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (right.peers().stream().noneMatch(
                peer -> peer.state()
                    == systems.zlink.contracts.service.spot
                        .MeshPeerState.ADMITTED)
                && System.nanoTime() < deadline) {
                Thread.sleep(1);
            }
            assertTrue(right.peers().stream().anyMatch(
                peer -> peer.state()
                    == systems.zlink.contracts.service.spot
                        .MeshPeerState.ADMITTED));

            List<Message> reply;
            try (Message request = Message.from("remote-actor-request")) {
                reply = right.spotNode().requestToActor(
                    actor,
                    List.of(request),
                    SendFlags.DONT_WAIT,
                    Duration.ofSeconds(2)).toCompletableFuture()
                    .get(2, TimeUnit.SECONDS);
            }
            try {
                assertEquals(
                    "remote-actor-reply",
                    reply.getFirst().toUtf8String());
            } finally {
                reply.forEach(Message::close);
            }
        }
    }

    @Test
    void streamBindingDispatchesThroughFrameworkActorAuthority()
        throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh");
             var stream = new ZLinkJavaStreamSocket(
                 context.createStreamSocket(), node)) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-stream-node");
            RoutingId sessionRid = RoutingId.from("jvm-m6b-session");
            node.setRoutingId(nodeRid);
            ZLinkBackendSpot entry = node.spotNode().entrySpot();
            CompletableFuture<RoutingId> delivered =
                new CompletableFuture<>();
            entry.onDispatchEvent(info -> {
                if (info.event()
                    != ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                    return;
                }
                var received = info.actorMessages().getFirst();
                delivered.complete(received.sourceSessionRid());
                info.actorMessages().forEach(
                    systems.zlink.framework.runtime.backend
                        .ZLinkBackendActorReceived::close);
            });
            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor;
            try (Message create = Message.from("create")) {
                actor = node.spotNode().createActor("actor-stream", create);
            }

            stream.startSessionService();
            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            try (Message message = Message.from("stream-ingress")) {
                assertTrue(stream.sendBoundActor(
                    sessionRid,
                    actor.actorId(),
                    List.of(message),
                    SendFlags.DONT_WAIT));
            }
            assertEquals(
                sessionRid,
                delivered.get(1, TimeUnit.SECONDS));

            stream.unbindActor(sessionRid, actor.actorId())
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            assertThrows(
                IllegalStateException.class,
                () -> stream.sendBoundActor(
                    sessionRid,
                    actor.actorId(),
                    List.of(),
                    SendFlags.DONT_WAIT));
        }
    }

    @Test
    void instanceSpotColdActivationJoinsAndPreservesStableType()
        throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            node.setRoutingId(RoutingId.from("jvm-m6b-instance-node"));
            ZLinkJavaRawSpotNode spots =
                (ZLinkJavaRawSpotNode) node.spotNode();
            RoutingId spotRid = RoutingId.from("jvm-m6b-instance");
            spots.registerInstanceSpotType("alpha");

            var first = spots.activateInstanceSpot(spotRid, null)
                .toCompletableFuture();
            var joined = spots.activateInstanceSpot(spotRid, "alpha")
                .toCompletableFuture();
            assertTrue(first.get(1, TimeUnit.SECONDS).spot()
                == joined.get(1, TimeUnit.SECONDS).spot());
            long firstGeneration =
                first.get(1, TimeUnit.SECONDS).spot().lifecycleGeneration();

            spots.registerInstanceSpotType("beta");
            assertThrows(
                IllegalStateException.class,
                () -> spots.activateInstanceSpot(
                    spotRid, "beta"));
            assertThrows(
                IllegalStateException.class,
                () -> spots.activateInstanceSpot(
                    RoutingId.from("jvm-m6b-instance-ambiguous"), null));

            assertTrue(spots.closeInstanceSpot(
                spotRid, firstGeneration));
            var reactivated = spots.activateInstanceSpot(spotRid, null)
                .toCompletableFuture().get(1, TimeUnit.SECONDS);
            assertEquals("alpha", reactivated.stableType());
            assertTrue(
                reactivated.spot().lifecycleGeneration()
                    > firstGeneration);
        }
    }
}
