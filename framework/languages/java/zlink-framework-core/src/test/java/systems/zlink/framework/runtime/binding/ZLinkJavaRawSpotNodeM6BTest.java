package systems.zlink.framework.runtime.binding;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
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
import systems.zlink.framework.runtime.service.ZLinkServiceM6BWireCodec;

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
    void remoteSpotAndActorRejectStaleAuthorityOwnerGeneration() {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId nodeRid = RoutingId.from("jvm-m6b-owner-node");
            RoutingId sourceRid = RoutingId.from("jvm-m6b-owner-source");
            RoutingId spotRid = RoutingId.from("jvm-m6b-owner-spot");
            node.setRoutingId(nodeRid);
            ZLinkJavaRawSpotNode spots =
                (ZLinkJavaRawSpotNode) node.spotNode();
            ZLinkBackendSpot spot = spots.createSpot(spotRid);
            spot.rememberSpotAuthority(
                nodeRid, spotRid, spot.lifecycleGeneration(), 31);
            var staleSpot = new ZLinkServiceM6BWireCodec.SpotMessage(
                false,
                0,
                null,
                sourceRid,
                new ZLinkServiceM6BWireCodec.SpotRouteFence(
                    spotRid,
                    spot.lifecycleGeneration(),
                    nodeRid,
                    1,
                    32));
            assertFalse(spots.enqueueRemoteSpot(
                sourceRid,
                staleSpot,
                new byte[0],
                List.of(),
                ignored -> { }));

            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef actor;
            try (Message create = Message.from("create")) {
                actor = spots.createActor("owner-actor", create);
            }
            spots.rememberActorAuthority(actor, 41);
            var staleActor = new ZLinkServiceM6BWireCodec.ActorMessage(
                false,
                0,
                null,
                null,
                new ZLinkServiceM6BWireCodec.ActorRouteFence(
                    actor,
                    1,
                    42));
            assertFalse(spots.enqueueRemoteActor(
                sourceRid,
                staleActor,
                List.of(),
                ignored -> { }));
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
            target.rememberSpotAuthority(
                leftRid,
                targetRid,
                target.lifecycleGeneration(),
                77);
            source.rememberSpotAuthority(
                leftRid,
                targetRid,
                target.lifecycleGeneration(),
                77);
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
            left.spotNode().rememberActorAuthority(actor, 89);
            right.spotNode().rememberActorAuthority(
                actor, 89);

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
    void logicalMulticastFansOutLocallyAndOncePerAdmittedRemoteNode()
        throws Exception {
        String endpoint = "inproc://jvm-m6b-multicast-" + System.nanoTime();
        RoutingId leftRid = RoutingId.from("jvm-m6b-multicast-left");
        RoutingId rightRid = RoutingId.from("jvm-m6b-multicast-right");
        try (var context = Zlink.createContext();
             var left = new ZLinkJavaRawMeshNode(context, "mesh");
             var right = new ZLinkJavaRawMeshNode(context, "mesh")) {
            left.setRoutingId(leftRid);
            left.setBind(endpoint);
            left.addChannel("events");
            right.setRoutingId(rightRid);
            right.setBind(
                "inproc://jvm-m6b-multicast-right-" + System.nanoTime());
            right.addChannel("events");
            left.start();
            right.start();
            right.connectPeer(endpoint, leftRid);
            ZLinkBackendSpot remote = left.spotNode().createSpot(
                RoutingId.from("jvm-m6b-multicast-target"));
            remote.setSubscription("orders");

            long deadline =
                System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (right.peers().stream().noneMatch(
                    peer -> peer.routingId().equals(leftRid)
                        && peer.state()
                            == systems.zlink.contracts.service.spot
                                .MeshPeerState.ADMITTED)
                && System.nanoTime() < deadline) {
                Thread.sleep(1);
            }
            assertTrue(right.peers().stream().anyMatch(
                peer -> peer.routingId().equals(leftRid)
                    && peer.state()
                        == systems.zlink.contracts.service.spot
                            .MeshPeerState.ADMITTED));

            var source = (ZLinkJavaRawSpot) right.spotNode().createSpot(
                RoutingId.from("jvm-m6b-multicast-source"));
            systems.zlink.contracts.service.spot.PublishDetail detail;
            try (Message packet = Message.from("Packet");
                 Message payload = Message.from("multicast")) {
                detail = right.publishLogicalMulticast(
                    source,
                    "events",
                    "orders",
                    new byte[] {7},
                    List.of(packet, payload));
            }
            assertEquals(1, detail.snapshotRemoteTargetCount());
            assertEquals(1, detail.admittedRemoteTargetCount());
            assertEquals(0, detail.droppedRemoteTargetCount());

            systems.zlink.framework.runtime.backend.ZLinkBackendTopicMessage
                received = null;
            while (received == null && System.nanoTime() < deadline) {
                received = remote.subscribe(
                    ZLinkBackendRecvMode.DONT_WAIT);
                if (received == null) {
                    Thread.sleep(1);
                }
            }
            assertNotNull(received);
            try {
                assertEquals("events", received.channelName());
                assertEquals("orders", received.topic());
                assertEquals(
                    "multicast",
                    received.parts().getLast().toUtf8String());
                assertEquals(7, received.applicationMetadata()[0]);
            } finally {
                received.parts().forEach(Message::close);
            }
        }
    }

    @Test
    void command39ActivatesOnlyTheRegisteredExactAuthorityFence()
        throws Exception {
        String endpoint = "inproc://jvm-m6b-instance-wire-"
            + System.nanoTime();
        RoutingId leftRid = RoutingId.from("jvm-m6b-instance-wire-left");
        RoutingId rightRid = RoutingId.from("jvm-m6b-instance-wire-right");
        RoutingId spotRid = RoutingId.from("jvm-m6b-instance-wire-spot");
        try (var context = Zlink.createContext();
             var left = new ZLinkJavaRawMeshNode(context, "mesh");
             var right = new ZLinkJavaRawMeshNode(context, "mesh")) {
            left.setRoutingId(leftRid);
            left.setBind(endpoint);
            right.setRoutingId(rightRid);
            right.setBind(
                "inproc://jvm-m6b-instance-wire-right-"
                    + System.nanoTime());
            left.start();
            right.start();
            right.connectPeer(endpoint, leftRid);

            long deadline =
                System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (right.peers().stream().noneMatch(
                    peer -> peer.routingId().equals(leftRid)
                        && peer.state()
                            == systems.zlink.contracts.service.spot
                                .MeshPeerState.ADMITTED)
                && System.nanoTime() < deadline) {
                Thread.sleep(1);
            }
            assertTrue(right.peers().stream().anyMatch(
                peer -> peer.routingId().equals(leftRid)
                    && peer.state()
                        == systems.zlink.contracts.service.spot
                            .MeshPeerState.ADMITTED));

            ZLinkJavaRawSpotNode target =
                (ZLinkJavaRawSpotNode) left.spotNode();
            target.registerInstanceSpotType("orders");
            var route = new ZLinkServiceM6BWireCodec.InstanceRouteFence(
                leftRid,
                left.lifecycleGeneration(),
                spotRid,
                41,
                "owner-a",
                17,
                9,
                "store-3");
            target.registerInstanceSpotAuthority("orders", route);

            try (Message packet = Message.from("Packet");
                 Message payload = Message.from("activate")) {
                assertTrue(right.sendInstanceSpot(
                    route,
                    null,
                    new byte[] {3},
                    List.of(packet, payload)));
            }

            ZLinkBackendSpot activated = null;
            while (activated == null && System.nanoTime() < deadline) {
                activated = target.localSpot(spotRid);
                if (activated == null) {
                    Thread.sleep(1);
                }
            }
            assertNotNull(activated);
            assertEquals(41, activated.lifecycleGeneration());
            var received = activated.recvRoute(
                ZLinkBackendRecvMode.DONT_WAIT);
            assertNotNull(received);
            try (received) {
                assertEquals(
                    "activate",
                    received.parts().getLast().toUtf8String());
                assertEquals(3, received.applicationMetadata()[0]);
            }

            var stale = new ZLinkServiceM6BWireCodec.InstanceRouteFence(
                leftRid,
                route.targetNodeGeneration(),
                spotRid,
                route.objectGeneration(),
                route.ownerId(),
                route.authorityOwnerGeneration() + 1,
                route.leaseGeneration(),
                route.storeVersion());
            try (Message packet = Message.from("Packet");
                 Message payload = Message.from("stale")) {
                assertTrue(right.sendInstanceSpot(
                    stale, null, new byte[0], List.of(packet, payload)));
            }
            Thread.sleep(20);
            assertEquals(
                null,
                activated.recvRoute(ZLinkBackendRecvMode.DONT_WAIT));
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
            long firstBindingGeneration =
                node.bindingGenerationSeed();
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

            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            try (Message stale = Message.from("stale-local-ingress")) {
                assertFalse(((ZLinkJavaRawSpotNode) node.spotNode())
                    .forwardBoundStreamSession(
                        actor,
                        sessionRid,
                        firstBindingGeneration,
                        2,
                        stream,
                        List.of(stale)));
            }
            try (Message current = Message.from("current-local-ingress")) {
                assertTrue(stream.sendBoundActor(
                    sessionRid,
                    actor.actorId(),
                    List.of(current),
                    SendFlags.DONT_WAIT));
            }

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
    void remoteBindingIdentityFencesOldOwnerEpochWithoutGlobalCounter()
        throws Exception {
        try (var context = Zlink.createContext();
             var actorNode = new ZLinkJavaRawMeshNode(context, "mesh")) {
            RoutingId actorNodeRid =
                RoutingId.from("jvm-m6b-identity-actor-node");
            actorNode.setRoutingId(actorNodeRid);
            actorNode.setBind(
                "inproc://jvm-m6b-identity-" + System.nanoTime());
            actorNode.start();
            ZLinkJavaRawSpotNode target =
                (ZLinkJavaRawSpotNode) actorNode.spotNode();
            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef
                actor;
            try (Message create = Message.from("create")) {
                actor = target.createActor("actor-owner-epoch", create);
            }
            target.rememberActorAuthority(actor, 41);
            var route =
                new ZLinkServiceM6BWireCodec.ActorRouteFence(
                    actor,
                    actorNode.lifecycleGeneration(),
                    41);
            RoutingId ownerA = RoutingId.from("session-owner-a");
            RoutingId ownerB = RoutingId.from("session-owner-b");
            RoutingId sessionA = RoutingId.from("session-a");
            RoutingId sessionB = RoutingId.from("session-b");

            assertTrue(target.acceptRemoteStreamBinding(
                ownerA,
                90,
                new ZLinkServiceM6BWireCodec.BoundSessionBind(
                    1, route, sessionA, true, 50)));
            assertTrue(target.acceptRemoteStreamBinding(
                ownerB,
                1,
                new ZLinkServiceM6BWireCodec.BoundSessionBind(
                    2, route, sessionB, true, 1)));
            assertTrue(target.acceptRemoteStreamBinding(
                ownerA,
                90,
                new ZLinkServiceM6BWireCodec.BoundSessionBind(
                    3, route, sessionA, false, 50)));
            assertBoundIngressRejected(
                target, ownerA, 90, route, sessionA, 50, 1);
            assertBoundIngressAccepted(
                target, ownerB, 1, route, sessionB, 1, 1);

            assertTrue(target.acceptRemoteStreamBinding(
                ownerB,
                2,
                new ZLinkServiceM6BWireCodec.BoundSessionBind(
                    4, route, sessionB, true, 1)));
            assertTrue(target.acceptRemoteStreamBinding(
                ownerB,
                1,
                new ZLinkServiceM6BWireCodec.BoundSessionBind(
                    5, route, sessionB, false, 1)));
            assertBoundIngressRejected(
                target, ownerB, 1, route, sessionB, 1, 2);
            assertBoundIngressAccepted(
                target, ownerB, 2, route, sessionB, 1, 1);
        }
    }

    @Test
    void streamCloseWaitsForSlowUnbindWithinTheLifecycleBound()
        throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh");
             var stream = new ZLinkJavaStreamSocket(
                 context.createStreamSocket(),
                 node,
                 null,
                 delayedUnbind(false, 100))) {
            node.setRoutingId(RoutingId.from("slow-unbind-node"));
            stream.startSessionService();
            RoutingId sessionRid = RoutingId.from("slow-unbind-session");
            var actor =
                new systems.zlink.framework.runtime.backend
                    .ZLinkBackendActorRef(
                        RoutingId.from("slow-unbind-actor-node"),
                        "slow-unbind-actor",
                        1);
            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);

            long started = System.nanoTime();
            stream.close();
            long elapsedMillis = TimeUnit.NANOSECONDS.toMillis(
                System.nanoTime() - started);
            assertTrue(elapsedMillis >= 75);
            assertTrue(elapsedMillis < 500);
        }
    }

    @Test
    void streamCloseObservesFailedUnbindWithoutBlockingIndefinitely()
        throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh")) {
            node.setRoutingId(RoutingId.from("failed-unbind-node"));
            var stream = new ZLinkJavaStreamSocket(
                context.createStreamSocket(),
                node,
                null,
                delayedUnbind(true, 50));
            stream.startSessionService();
            RoutingId sessionRid =
                RoutingId.from("failed-unbind-session");
            var actor =
                new systems.zlink.framework.runtime.backend
                    .ZLinkBackendActorRef(
                        RoutingId.from("failed-unbind-actor-node"),
                        "failed-unbind-actor",
                        1);
            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);

            long started = System.nanoTime();
            assertThrows(IllegalStateException.class, stream::close);
            long elapsedMillis = TimeUnit.NANOSECONDS.toMillis(
                System.nanoTime() - started);
            assertTrue(elapsedMillis >= 25);
            assertTrue(elapsedMillis < 500);
            stream.close();
        }
    }

    @Test
    void streamClosePreservesCleanupAndNativeCloseFailures()
        throws Exception {
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh");
             var raw = context.createStreamSocket()) {
            node.setRoutingId(RoutingId.from("dual-close-node"));
            var stream = new ZLinkJavaStreamSocket(
                raw,
                node,
                null,
                delayedUnbind(true, 10),
                () -> {
                    raw.close();
                    throw new IllegalStateException(
                        "simulated native close failure");
                });
            stream.startSessionService();
            RoutingId sessionRid = RoutingId.from("dual-close-session");
            var actor =
                new systems.zlink.framework.runtime.backend
                    .ZLinkBackendActorRef(
                        RoutingId.from("dual-close-actor-node"),
                        "dual-close-actor",
                        1);
            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);

            IllegalStateException failure = assertThrows(
                IllegalStateException.class,
                stream::close);
            assertEquals(
                "STREAM binding cleanup did not complete",
                failure.getMessage());
            assertEquals(
                "simulated unbind failure",
                failure.getCause().getMessage());
            assertEquals(1, failure.getSuppressed().length);
            assertEquals(
                "simulated native close failure",
                failure.getSuppressed()[0].getMessage());
        }
    }

    @Test
    void streamCloseAggregatesSynchronousErrorAndContinuesCleanup()
        throws Exception {
        AtomicInteger unbindAttempts = new AtomicInteger();
        AtomicBoolean nativeCloseAttempted = new AtomicBoolean();
        try (var context = Zlink.createContext();
             var node = new ZLinkJavaRawMeshNode(context, "mesh");
             var raw = context.createStreamSocket()) {
            node.setRoutingId(RoutingId.from("error-close-node"));
            ZLinkJavaStreamSocket.BoundSessionLifecycle lifecycle =
                new ZLinkJavaStreamSocket.BoundSessionLifecycle() {
                    @Override
                    public CompletionStage<Void> bind(
                        RoutingId sessionRid,
                        systems.zlink.framework.runtime.backend
                            .ZLinkBackendActorRef actor,
                        long bindingGeneration,
                        Duration timeout) {
                        return CompletableFuture.completedFuture(null);
                    }

                    @Override
                    public CompletionStage<Void> unbind(
                        RoutingId sessionRid,
                        systems.zlink.framework.runtime.backend
                            .ZLinkBackendActorRef actor,
                        long bindingGeneration,
                        Duration timeout) {
                        if (unbindAttempts.incrementAndGet() == 1) {
                            throw new AssertionError(
                                "simulated synchronous unbind error");
                        }
                        return CompletableFuture.completedFuture(null);
                    }
                };
            var stream = new ZLinkJavaStreamSocket(
                raw,
                node,
                null,
                lifecycle,
                () -> {
                    nativeCloseAttempted.set(true);
                    raw.close();
                    throw new IllegalStateException(
                        "simulated native close after Error");
                });
            stream.startSessionService();
            RoutingId sessionRid = RoutingId.from("error-close-session");
            for (int index = 1; index <= 2; index++) {
                var actor =
                    new systems.zlink.framework.runtime.backend
                        .ZLinkBackendActorRef(
                            RoutingId.from("error-close-actor-node"),
                            "error-close-actor-" + index,
                            1);
                stream.bindActor(sessionRid, actor)
                    .submit(Duration.ofSeconds(1)).toCompletableFuture()
                    .get(1, TimeUnit.SECONDS);
            }

            IllegalStateException failure = assertThrows(
                IllegalStateException.class,
                stream::close);
            assertEquals(2, unbindAttempts.get());
            assertTrue(nativeCloseAttempted.get());
            assertTrue(failure.getCause() instanceof AssertionError);
            assertEquals(
                "simulated synchronous unbind error",
                failure.getCause().getMessage());
            assertEquals(1, failure.getSuppressed().length);
            assertEquals(
                "simulated native close after Error",
                failure.getSuppressed()[0].getMessage());
        }
    }

    private static ZLinkJavaStreamSocket.BoundSessionLifecycle
        delayedUnbind(boolean fail, long delayMillis) {
        return new ZLinkJavaStreamSocket.BoundSessionLifecycle() {
            @Override
            public CompletionStage<Void> bind(
                RoutingId sessionRid,
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendActorRef actor,
                long bindingGeneration,
                Duration timeout) {
                return CompletableFuture.completedFuture(null);
            }

            @Override
            public CompletionStage<Void> unbind(
                RoutingId sessionRid,
                systems.zlink.framework.runtime.backend
                    .ZLinkBackendActorRef actor,
                long bindingGeneration,
                Duration timeout) {
                CompletableFuture<Void> result =
                    new CompletableFuture<>();
                CompletableFuture.delayedExecutor(
                    delayMillis,
                    TimeUnit.MILLISECONDS).execute(() -> {
                        if (fail) {
                            result.completeExceptionally(
                                new IllegalStateException(
                                    "simulated unbind failure"));
                        } else {
                            result.complete(null);
                        }
                    });
                return result;
            }
        };
    }

    private static void assertBoundIngressAccepted(
        ZLinkJavaRawSpotNode target,
        RoutingId ownerRid,
        long ownerGeneration,
        ZLinkServiceM6BWireCodec.ActorRouteFence route,
        RoutingId sessionRid,
        long bindingGeneration,
        long sequence) {
        int flags =
            systems.zlink.framework.runtime.protocol
                .ServiceWireConstants.FLAG_BOUND_SESSION
                | systems.zlink.framework.runtime.protocol
                    .ServiceWireConstants.FLAG_SOURCE_SPOT_RID;
        Message payload = Message.from("bound-ingress");
        boolean accepted = target.enqueueRemoteActor(
                ownerRid,
                ownerGeneration,
                new ZLinkServiceM6BWireCodec.ActorMessage(
                    false,
                    flags,
                    null,
                    null,
                    route,
                    new ZLinkServiceM6BWireCodec.BoundSessionTail(
                        sessionRid,
                        bindingGeneration,
                        sequence)),
                List.of(payload),
                null);
        if (!accepted) {
            payload.close();
        }
        assertTrue(accepted);
    }

    private static void assertBoundIngressRejected(
        ZLinkJavaRawSpotNode target,
        RoutingId ownerRid,
        long ownerGeneration,
        ZLinkServiceM6BWireCodec.ActorRouteFence route,
        RoutingId sessionRid,
        long bindingGeneration,
        long sequence) {
        int flags =
            systems.zlink.framework.runtime.protocol
                .ServiceWireConstants.FLAG_BOUND_SESSION
                | systems.zlink.framework.runtime.protocol
                    .ServiceWireConstants.FLAG_SOURCE_SPOT_RID;
        try (Message payload = Message.from("stale-bound-ingress")) {
            assertFalse(target.enqueueRemoteActor(
                ownerRid,
                ownerGeneration,
                new ZLinkServiceM6BWireCodec.ActorMessage(
                    false,
                    flags,
                    null,
                    null,
                    route,
                    new ZLinkServiceM6BWireCodec.BoundSessionTail(
                        sessionRid,
                        bindingGeneration,
                        sequence)),
                List.of(payload),
                null));
        }
    }

    @Test
    void remoteBoundStreamUsesRawMeshAndExactGenerationFences()
        throws Exception {
        String endpoint = "inproc://jvm-m6b-bound-stream-"
            + System.nanoTime();
        RoutingId actorNodeRid =
            RoutingId.from("jvm-m6b-bound-actor-node");
        RoutingId sessionNodeRid =
            RoutingId.from("jvm-m6b-bound-session-node");
        RoutingId sessionRid =
            RoutingId.from("jvm-m6b-bound-session");
        CopyOnWriteArrayList<String> pushed =
            new CopyOnWriteArrayList<>();
        try (var context = Zlink.createContext();
             var actorNode = new ZLinkJavaRawMeshNode(context, "mesh");
             var sessionNode = new ZLinkJavaRawMeshNode(context, "mesh");
             var stream = new ZLinkJavaStreamSocket(
                 context.createStreamSocket(),
                 sessionNode,
                 (rid, parts, flags) -> {
                     assertEquals(sessionRid, rid);
                     pushed.add(parts.getLast().toUtf8String());
                     return true;
                 })) {
            actorNode.setRoutingId(actorNodeRid);
            actorNode.setBind(endpoint);
            sessionNode.setRoutingId(sessionNodeRid);
            sessionNode.setBind(
                "inproc://jvm-m6b-bound-session-owner-"
                    + System.nanoTime());
            actorNode.start();
            sessionNode.start();
            sessionNode.connectPeer(endpoint, actorNodeRid);

            ZLinkBackendSpot entry = actorNode.spotNode().entrySpot();
            CompletableFuture<RoutingId> ingress =
                new CompletableFuture<>();
            entry.onDispatchEvent(info -> {
                if (info.event()
                    != ZLinkBackendSpotDispatchEvent.ACTOR_READABLE) {
                    return;
                }
                var received = info.actorMessages().getFirst();
                ingress.complete(received.sourceSessionRid());
                info.actorMessages().forEach(
                    systems.zlink.framework.runtime.backend
                        .ZLinkBackendActorReceived::close);
            });
            systems.zlink.framework.runtime.backend.ZLinkBackendActorRef
                actor;
            try (Message create = Message.from("create")) {
                actor = actorNode.spotNode().createActor(
                    "actor-bound-remote", create);
            }
            actorNode.spotNode().rememberActorAuthority(actor, 73);
            sessionNode.spotNode().rememberActorAuthority(actor, 73);

            long deadline =
                System.nanoTime() + Duration.ofSeconds(2).toNanos();
            while (sessionNode.peers().stream().noneMatch(
                    peer -> peer.routingId().equals(actorNodeRid)
                        && peer.state()
                            == systems.zlink.contracts.service.spot
                                .MeshPeerState.ADMITTED)
                && System.nanoTime() < deadline) {
                Thread.sleep(1);
            }
            assertTrue(sessionNode.peers().stream().anyMatch(
                peer -> peer.routingId().equals(actorNodeRid)
                    && peer.state()
                        == systems.zlink.contracts.service.spot
                            .MeshPeerState.ADMITTED));

            stream.startSessionService();
            long firstBindingGeneration =
                sessionNode.bindingGenerationSeed();
            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            try (Message message = Message.from("ingress")) {
                assertTrue(stream.sendBoundActor(
                    sessionRid,
                    actor.actorId(),
                    List.of(message),
                    SendFlags.DONT_WAIT));
            }
            assertEquals(sessionRid, ingress.get(1, TimeUnit.SECONDS));

            sessionNode.spotNode().rememberActorAuthority(actor, 74);
            try (Message push = Message.from("push-one")) {
                assertTrue(actorNode.spotNode().sendActorBoundSession(
                    actor, List.of(push), SendFlags.DONT_WAIT));
            }
            while (pushed.isEmpty()
                && System.nanoTime() < deadline) {
                Thread.sleep(1);
            }
            assertEquals(List.of("push-one"), pushed);
            sessionNode.spotNode().rememberActorAuthority(actor, 73);

            stream.bindActor(sessionRid, actor)
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            ZLinkJavaRawSpotNode source =
                (ZLinkJavaRawSpotNode) sessionNode.spotNode();
            ZLinkJavaRawSpotNode target =
                (ZLinkJavaRawSpotNode) actorNode.spotNode();
            var currentRoute =
                new ZLinkServiceM6BWireCodec.ActorRouteFence(
                    actor,
                    actorNode.lifecycleGeneration(),
                    73);
            try (Message packet = Message.from("message");
                 Message payload = Message.from("stale")) {
                assertFalse(source.acceptBoundSessionPush(
                    actorNodeRid,
                    actorNode.lifecycleGeneration(),
                    new ZLinkServiceM6BWireCodec.BoundSessionSend(
                        currentRoute, firstBindingGeneration),
                    List.of(packet, payload)));
                assertFalse(source.acceptBoundSessionPush(
                    actorNodeRid,
                    actorNode.lifecycleGeneration() + 1,
                    new ZLinkServiceM6BWireCodec.BoundSessionSend(
                        currentRoute, firstBindingGeneration + 1),
                    List.of(packet, payload)));
                assertFalse(source.acceptBoundSessionPush(
                    actorNodeRid,
                    actorNode.lifecycleGeneration(),
                    new ZLinkServiceM6BWireCodec.BoundSessionSend(
                        new ZLinkServiceM6BWireCodec.ActorRouteFence(
                            new systems.zlink.framework.runtime.backend
                                .ZLinkBackendActorRef(
                                    actorNodeRid,
                                    actor.actorId(),
                                    actor.generation() + 1),
                            actorNode.lifecycleGeneration(),
                            73),
                        firstBindingGeneration + 1),
                    List.of(packet, payload)));
                assertFalse(source.acceptBoundSessionPush(
                    actorNodeRid,
                    actorNode.lifecycleGeneration(),
                    new ZLinkServiceM6BWireCodec.BoundSessionSend(
                        new ZLinkServiceM6BWireCodec.ActorRouteFence(
                            actor,
                            actorNode.lifecycleGeneration(),
                            74),
                        firstBindingGeneration + 1),
                    List.of(packet, payload)));
            }

            int boundFlags =
                systems.zlink.framework.runtime.protocol
                    .ServiceWireConstants.FLAG_BOUND_SESSION
                    | systems.zlink.framework.runtime.protocol
                        .ServiceWireConstants.FLAG_SOURCE_SPOT_RID;
            try (Message payload = Message.from("stale-ingress")) {
                assertFalse(target.enqueueRemoteActor(
                    sessionNodeRid,
                    sessionNode.lifecycleGeneration(),
                    new ZLinkServiceM6BWireCodec.ActorMessage(
                        false,
                        boundFlags,
                        null,
                        null,
                        currentRoute,
                        new ZLinkServiceM6BWireCodec.BoundSessionTail(
                            sessionRid,
                            firstBindingGeneration,
                            10)),
                    List.of(payload),
                    null));
                assertFalse(target.enqueueRemoteActor(
                    sessionNodeRid,
                    sessionNode.lifecycleGeneration() + 1,
                    new ZLinkServiceM6BWireCodec.ActorMessage(
                        false,
                        boundFlags,
                        null,
                        null,
                        currentRoute,
                        new ZLinkServiceM6BWireCodec.BoundSessionTail(
                            sessionRid,
                            firstBindingGeneration + 1,
                            11)),
                    List.of(payload),
                    null));
            }

            assertTrue(target.acceptRemoteStreamBinding(
                sessionNodeRid,
                sessionNode.lifecycleGeneration(),
                new ZLinkServiceM6BWireCodec.BoundSessionBind(
                    999,
                    currentRoute,
                    sessionRid,
                    false,
                    firstBindingGeneration)));
            try (Message push = Message.from("push-two")) {
                assertTrue(actorNode.spotNode().sendActorBoundSession(
                    actor, List.of(push), SendFlags.DONT_WAIT));
            }
            while (pushed.size() < 2
                && System.nanoTime() < deadline) {
                Thread.sleep(1);
            }
            assertEquals(
                List.of("push-one", "push-two"), pushed);

            stream.unbindActor(sessionRid, actor.actorId())
                .submit(Duration.ofSeconds(1)).toCompletableFuture()
                .get(1, TimeUnit.SECONDS);
            try (Message late = Message.from("late")) {
                assertFalse(actorNode.spotNode().sendActorBoundSession(
                    actor, List.of(late), SendFlags.DONT_WAIT));
            }
            assertEquals(2, pushed.size());
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
