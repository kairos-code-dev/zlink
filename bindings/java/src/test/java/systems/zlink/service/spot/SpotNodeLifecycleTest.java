package systems.zlink.contracts.service.spot;

import systems.zlink.TestSupport;
import systems.zlink.contracts.sockets.AutoHwmProfile;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.core.RoutingId;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

class SpotNodeLifecycleTest {
    @Test
    void spotNodeActorRefSendAndRequestSurfaceExists() throws Exception {
        assertEquals(SendOperation.class,
            SpotNode.class.getMethod("sendToActor", ActorRef.class).getReturnType());
        assertEquals(RequestOperation.class,
            SpotNode.class.getMethod("requestToActor", ActorRef.class).getReturnType());
    }

    @Test
    void closeCascadesToOwnedSpots() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext()) {
            SpotNode node = ctx.createSpotNode();
            Spot first = node.createSpot();
            Spot second = node.createSpot();

            assertDoesNotThrow(first::getRoutingId);
            assertDoesNotThrow(second::getRoutingId);

            node.close();

            assertThrows(IllegalStateException.class, first::getRoutingId);
            assertThrows(IllegalStateException.class, second::getRoutingId);
            assertDoesNotThrow(first::close);
            assertDoesNotThrow(second::close);
        }
    }

    @Test
    void spotLookupAndOptionFacadesUseCoreEntrypoints() {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode()) {
            Spot entry = node.entrySpot();
            assertNotNull(entry);

            Spot spot = node.createSpot();
            RoutingId rid = RoutingId.from(new byte[] {1, 2, 3, 4});
            spot.setRoutingId(rid);
            Spot lookup = node.spotLookup(rid).orElseThrow();
            assertNotNull(lookup);
            assertTrue(node.spotLookup(RoutingId.from(new byte[] {9, 9}))
                .isEmpty());

            node.routerHwmProfile(AutoHwmProfile.BALANCED);
            assertEquals(AutoHwmProfile.BALANCED, node.routerHwmProfile());
            node.routerHighWaterMark(128);
            assertEquals(128, node.routerHighWaterMark());
            node.pubSubHwmProfile(AutoHwmProfile.LOW_LATENCY);
            assertEquals(AutoHwmProfile.LOW_LATENCY, node.pubSubHwmProfile());
            node.pubSubHighWaterMark(64);
            assertEquals(64, node.pubSubHighWaterMark());

            spot.requestTimeout(Duration.ofMillis(123));
            assertEquals(Duration.ofMillis(123), spot.requestTimeout());
        }
    }

    @Test
    void entrySpotJoinUsesAdmissionRequestAndReply() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode(SpotNodeMode.ALL)) {
            Spot userSpot = node.createSpot();
            Spot entrySpot = node.entrySpot();
            Actor actor = node.createActor("java-entry-join");
            CompletableFuture<String> userRequest = new CompletableFuture<>();
            java.util.concurrent.atomic.AtomicReference<CompletableFuture<String>>
                entryRequest = new java.util.concurrent.atomic.AtomicReference<>(
                    new CompletableFuture<>());
            java.util.concurrent.atomic.AtomicBoolean rejectEntryJoin =
                new java.util.concurrent.atomic.AtomicBoolean(false);

            userSpot.setDispatchHandler(info -> {
                if (info.event() != SpotDispatchEvent.ACTOR_JOIN_READABLE)
                    return;
                try (ActorJoinRequest request =
                         userSpot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                    userRequest.complete(request.message().toUtf8String());
                    userSpot.replyActorJoin(request, 0)
                        .message(Message.from("user-reply"))
                        .submit();
                }
            });

            entrySpot.setDispatchHandler(info -> {
                if (info.event() != SpotDispatchEvent.ACTOR_JOIN_READABLE)
                    return;
                try (ActorJoinRequest request =
                         entrySpot.recvActorJoin(RecvFlags.DONT_WAIT)) {
                    entryRequest.get().complete(request.message().toUtf8String());
                    entrySpot.replyActorJoin(
                            request,
                            rejectEntryJoin.get() ? 7 : 0)
                        .message(Message.from(
                            rejectEntryJoin.get()
                                ? "entry-rejected"
                                : "entry-reply"))
                        .submit();
                }
            });

            ActorJoinCompletion userJoin = actor.join(userSpot)
                .message(Message.from("user-join-request"))
                .timeout(Duration.ofSeconds(1))
                .submit()
                .toCompletableFuture()
                .get(2, TimeUnit.SECONDS);
            assertEquals(RequestResult.OK, userJoin.result().result());
            assertEquals("user-join-request",
                userRequest.get(2, TimeUnit.SECONDS));

            rejectEntryJoin.set(true);
            ActorJoinEntrySpotCompletion rejectedEntryJoin = node.joinActorEntrySpot(
                    actor.ref(), node.getRoutingId(),
                    Message.from("entry-reject-request"))
                .timeout(Duration.ofSeconds(1))
                .submit()
                .toCompletableFuture()
                .get(2, TimeUnit.SECONDS);

            assertEquals(RequestResult.OK, rejectedEntryJoin.result().result());
            assertEquals(7, rejectedEntryJoin.result().joinResultCode());
            assertEquals("entry-reject-request",
                entryRequest.get().get(2, TimeUnit.SECONDS));
            assertEquals(1, rejectedEntryJoin.replyParts().size());
            assertEquals("entry-rejected",
                rejectedEntryJoin.replyParts().get(0).toUtf8String());
            assertEquals(1, userSpot.actors().size());
            assertEquals(actor.ref().actorId(), userSpot.actors().get(0).actorId());

            rejectEntryJoin.set(false);
            entryRequest.set(new CompletableFuture<>());
            ActorJoinEntrySpotCompletion entryJoin = node.joinActorEntrySpot(
                    actor.ref(), node.getRoutingId(),
                    Message.from("entry-join-request"))
                .timeout(Duration.ofSeconds(1))
                .submit()
                .toCompletableFuture()
                .get(2, TimeUnit.SECONDS);

            assertEquals(RequestResult.OK, entryJoin.result().result());
            assertEquals(0, entryJoin.result().joinResultCode());
            assertEquals("entry-join-request",
                entryRequest.get().get(2, TimeUnit.SECONDS));
            assertEquals(1, entryJoin.replyParts().size());
            assertEquals("entry-reply",
                entryJoin.replyParts().get(0).toUtf8String());
        }
    }

    @Test
    void actorCreateRequestPayloadIsExposedOnEntrySpotLifecycle() throws Exception {
        TestSupport.assumeNative();

        try (Context ctx = Zlink.createContext();
             SpotNode node = ctx.createSpotNode(SpotNodeMode.ALL)) {
            Spot entrySpot = node.entrySpot();
            CompletableFuture<SpotActorLifecycleEvent> created =
                new CompletableFuture<>();

            entrySpot.setDispatchHandler(info -> {
                if (info.event() != SpotDispatchEvent.ACTOR_LIFECYCLE_READABLE)
                    return;
                SpotActorLifecycleEvent event =
                    entrySpot.recvActorLifecycle(RecvFlags.DONT_WAIT);
                if (event != null) {
                    created.complete(event);
                }
            });

            Message profile = Message.from("profile");
            Message displayName = Message.from("display-name");
            Actor actor = node.createActor(
                "java-create-payload",
                java.util.List.of(profile, displayName));

            assertEquals(0, profile.size());
            assertEquals(0, displayName.size());

            try (SpotActorLifecycleEvent event =
                     created.get(2, TimeUnit.SECONDS)) {
                assertEquals(SpotActorLifecycleEventKind.JOINED, event.kind());
                assertEquals(actor.ref().actorId(),
                    event.info().currentActor().actorId());
                assertEquals(2, event.requestParts().size());
                assertEquals("profile",
                    event.requestParts().get(0).toUtf8String());
                assertEquals("display-name",
                    event.requestParts().get(1).toUtf8String());
            }
        }
    }

}
