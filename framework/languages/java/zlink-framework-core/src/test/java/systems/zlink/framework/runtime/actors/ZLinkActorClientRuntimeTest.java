package systems.zlink.framework.runtime.actors;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletionException;
import java.util.concurrent.CompletionStage;
import org.junit.jupiter.api.Test;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ConfigResult;
import systems.zlink.contracts.errors.ZlinkConfigException;
import systems.zlink.contracts.errors.ZlinkRequestException;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.sockets.RequestResult;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.contracts.sockets.SubmitResult;
import systems.zlink.framework.channels.ZLinkSubmitStatus;
import systems.zlink.framework.actors.ActorRef;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.internal.backend.ZLinkInternalSpotNode;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotRouteBridge;
import systems.zlink.framework.runtime.locations.ZLinkInMemoryLocationStore;
import systems.zlink.framework.runtime.locations.ZLinkRegisteredLocationStores;
import systems.zlink.framework.runtime.locations.ZLinkStoreLocationResolvers;
import systems.zlink.framework.runtime.messaging.ZLinkJsonMessageSerializer;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeader;
import systems.zlink.framework.runtime.streams.ZLinkStreamHeaderCodec;
import systems.zlink.framework.spots.ZLinkSpotKind;

final class ZLinkActorClientRuntimeTest {
    @Test
    void sendAndRequestUseActorRefAndUseBackendNoBindOperations() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        store.claimOwnerLease("owner", Duration.ofMinutes(1))
            .toCompletableFuture()
            .join();
        store.updateActor(
                new ZLinkActorLocation(
                    "actor-1",
                    "test",
                    new ActorRef(RoutingId.from("actor-node"), "actor-1", 7),
                    RoutingId.from("actor-node"),
                    ZLinkSpotKind.ENTRY,
                    "mesh",
                    RoutingId.from("actor-node"),
                    "owner",
                    1,
                    Instant.now()),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        RecordingSpotNode node = new RecordingSpotNode(reply("pong"));
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> node,
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(store),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofSeconds(5));

        ActorRef actorRef = new ActorRef(RoutingId.from("actor-node"), "actor-1", 7);
        client.sendToActor(actorRef, new Ping("hello"))
            .submit();
        Pong pong = client.requestToActor(actorRef, new Ping("hello"))
            .submit(Pong.class)
            .toCompletableFuture()
            .join();

        assertEquals("actor-1", node.sentActor.actorId());
        assertEquals(7, node.sentActor.generation());
        assertEquals("actor-1", node.requestedActor.actorId());
        assertEquals("pong", pong.value());
    }

    @Test
    void actorRefSendAndRequestUseProvidedRefWithoutLocationResolve() {
        RecordingSpotNode node = new RecordingSpotNode(reply("pong"));
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> node,
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(new ZLinkInMemoryLocationStore()),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofSeconds(5));
        ActorRef actorRef = new ActorRef(RoutingId.from("direct-node"), "actor-direct", 17);

        client.sendToActor(actorRef, new Ping("hello"))
            .submit();
        Pong pong = client.requestToActor(actorRef, new Ping("hello"))
            .submit(Pong.class)
            .toCompletableFuture()
            .join();

        assertEquals("actor-direct", node.sentActor.actorId());
        assertEquals(RoutingId.from("direct-node"), node.sentActor.nodeRid());
        assertEquals(17, node.sentActor.generation());
        assertEquals("actor-direct", node.requestedActor.actorId());
        assertEquals("pong", pong.value());
    }

    @Test
    void actorManagerFindReturnsEmptyWhenNativeLookupReportsNotFound() {
        ZLinkActorRuntime actors = new ZLinkActorRuntime(
            new MissingLookupSpotNode(),
            java.util.Map.of("probe", ProbeActorFactory.class),
            Duration.ofSeconds(5),
            new ZLinkJsonMessageSerializer());

        assertEquals(
            java.util.Optional.empty(),
            actors.find("missing").toCompletableFuture().join());
    }

    @Test
    void actorManagerFindReturnsEmptyWhenMeshLookupReturnsNoActor() {
        ZLinkActorRuntime actors = new ZLinkActorRuntime(
            new EmptyLookupSpotNode(),
            java.util.Map.of("probe", ProbeActorFactory.class),
            Duration.ofSeconds(5),
            new ZLinkJsonMessageSerializer());

        assertEquals(
            java.util.Optional.empty(),
            actors.find("missing").toCompletableFuture().join());
    }

    @Test
    void explicitActorSendReturnsRouteNotConnectedWithoutPolling() {
        ZLinkInMemoryLocationStore store = storeWithActor("actor-1");
        RecordingSpotNode node = new RecordingSpotNode();
        node.sendFailure = SubmitResult.NOT_CONNECTED;
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> node,
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(store),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofMillis(250));

        var result = client.sendToActor(
                new ActorRef(RoutingId.from("actor-node"), "actor-1", 7),
                new Ping("hello"))
            .submit()
            .toCompletableFuture()
            .join();

        assertEquals(ZLinkSubmitStatus.ROUTE_NOT_CONNECTED, result.status());
        assertEquals(1, node.sendAttempts);
    }

    @Test
    void explicitActorSendPreservesTargetNotFoundAndShutdownResults() {
        for (var expected : List.of(
            java.util.Map.entry(SubmitResult.NOT_FOUND, ZLinkSubmitStatus.TARGET_NOT_FOUND),
            java.util.Map.entry(SubmitResult.TERMINATED, ZLinkSubmitStatus.SHUTDOWN))) {
            RecordingSpotNode node = new RecordingSpotNode();
            node.sendFailure = expected.getKey();
            ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
                () -> node,
                new ZLinkStoreLocationResolvers(
                    ZLinkRegisteredLocationStores.fromUnified(storeWithActor("actor-1")),
                    new systems.zlink.framework.locations.ZLinkLocationOptions()),
                new ZLinkJsonMessageSerializer(),
                Duration.ofSeconds(5));

            var result = client.sendToActor(
                    new ActorRef(RoutingId.from("actor-node"), "actor-1", 7),
                    new Ping("hello"))
                .submit()
                .toCompletableFuture()
                .join();

            assertEquals(expected.getValue(), result.status());
            assertEquals(1, node.sendAttempts);
        }
    }

    @Test
    void nativeRequestConflictMapsToActorLocationStaleAfterReresolve() {
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> new RequestFailingSpotNode(RequestResult.CONFLICT),
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(storeWithActor("actor-1")),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofSeconds(5));

        CompletionException error = assertThrows(
            CompletionException.class,
            () -> client.requestToActor(new ActorRef(RoutingId.from("actor-node"), "actor-1", 7), new Ping("hello"))
                .submit(Pong.class)
                .toCompletableFuture()
                .join());

        ZLinkFrameworkException frameworkError = (ZLinkFrameworkException) error.getCause();
        assertEquals(ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE, frameworkError.kind());
    }

    @Test
    void inactiveExplicitActorRefMapsNotFoundToActorRouteNotFound() {
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> new RequestFailingSpotNode(RequestResult.NOT_FOUND),
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(storeWithActor("actor-1")),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofSeconds(5));

        CompletionException error = assertThrows(
            CompletionException.class,
            () -> client.requestToActor(
                    new ActorRef(RoutingId.from("actor-node"), "actor-1", 7),
                    new Ping("hello"))
                .submit(Pong.class)
                .toCompletableFuture()
                .join());

        ZLinkFrameworkException frameworkError = (ZLinkFrameworkException) error.getCause();
        assertEquals(ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND, frameworkError.kind());
    }

    @Test
    void supersededExplicitActorRefMapsNotFoundToActorLocationStale() {
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> new RequestFailingSpotNode(RequestResult.NOT_FOUND),
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(storeWithActor("actor-1", 8)),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofSeconds(5));

        CompletionException error = assertThrows(
            CompletionException.class,
            () -> client.requestToActor(
                    new ActorRef(RoutingId.from("actor-node"), "actor-1", 7),
                    new Ping("hello"))
                .submit(Pong.class)
                .toCompletableFuture()
                .join());

        ZLinkFrameworkException frameworkError = (ZLinkFrameworkException) error.getCause();
        assertEquals(ZLinkFrameworkErrorKind.ACTOR_LOCATION_STALE, frameworkError.kind());
    }

    private static ZLinkInMemoryLocationStore storeWithActor(String actorId) {
        return storeWithActor(actorId, 7);
    }

    private static ZLinkInMemoryLocationStore storeWithActor(String actorId, long generation) {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        store.claimOwnerLease("owner", Duration.ofMinutes(1))
            .toCompletableFuture()
            .join();
        store.updateActor(
                new ZLinkActorLocation(
                    actorId,
                    "test",
                    new ActorRef(RoutingId.from("actor-node"), actorId, generation),
                    RoutingId.from("actor-node"),
                    ZLinkSpotKind.ENTRY,
                    "mesh",
                    RoutingId.from("actor-node"),
                    "owner",
                    1,
                    Instant.now()),
                ZLinkLocationWriteIntent.NEW_CLAIM)
            .toCompletableFuture()
            .join();
        return store;
    }

    private static List<Message> reply(String value) {
        ZLinkJsonMessageSerializer serializer = new ZLinkJsonMessageSerializer();
        ZLinkStreamHeader header = new ZLinkStreamHeader(
            systems.zlink.framework.streams.ZLinkStreamMessageKind.RESPONSE,
            systems.zlink.framework.streams.ZLinkStreamCodec.JSON,
            java.util.EnumSet.noneOf(systems.zlink.framework.runtime.streams.ZLinkStreamHeaderFlag.class),
            java.util.Optional.of(1L),
            "Pong",
            java.util.Map.of());
        return List.of(
            Message.from(ZLinkStreamHeaderCodec.encode(header)),
            Message.from(serializer.serialize(new Pong(value)).bytes()));
    }

    private record Ping(String value) {
    }

    private record Pong(String value) {
    }

    public static final class ProbeActor implements systems.zlink.framework.actors.ZLinkActor {
        private final String actorId;

        ProbeActor(String actorId) {
            this.actorId = actorId;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public systems.zlink.framework.actors.ZLinkActorContext context() {
            return null;
        }
    }

    public static final class ProbeActorFactory implements systems.zlink.framework.actors.ZLinkActorFactory {
        @Override
        public CompletionStage<systems.zlink.framework.actors.ZLinkActor> create(
            String actorId,
            systems.zlink.framework.actors.ZLinkActorContext context) {
            return java.util.concurrent.CompletableFuture.completedFuture(new ProbeActor(actorId));
        }
    }

    private static final class MissingLookupSpotNode extends RecordingSpotNode {
        @Override
        public ZLinkBackendActorRef actorLookup(String actorId) {
            throw new ZlinkConfigException(ConfigResult.NOT_FOUND);
        }
    }

    private static final class EmptyLookupSpotNode extends RecordingSpotNode {
        @Override
        public ZLinkBackendActorRef actorLookup(String actorId) {
            return null;
        }
    }

    private static class RecordingSpotNode implements ZLinkInternalSpotNode {
        private final List<Message> reply;
        ZLinkBackendActorRef sentActor;
        ZLinkBackendActorRef requestedActor;
        int sendAttempts;
        SubmitResult sendFailure;

        RecordingSpotNode() {
            this(reply("unused"));
        }

        RecordingSpotNode(List<Message> reply) {
            this.reply = reply;
        }

        @Override public RoutingId routingId() { return RoutingId.from("caller"); }
        @Override public void setRoutingId(RoutingId routingId) { }
        @Override public void setPublisherRoutingId(RoutingId routingId) { }
        @Override public void setSubscriberRoutingId(RoutingId routingId) { }
        @Override public void setRouterBind(String endpoint) { }
        @Override public void setPubBind(String endpoint) { }
        @Override public void connectPeer(String endpoint) { }
        @Override public void connectPeer(RoutingId peerRid, String endpoint) { }
        @Override public void disconnectPeer(String endpoint) { }
        @Override public void disconnectPeer(RoutingId peerRid) { }
        @Override public ZLinkBackendSpotRouteBridge createRouteBridge() { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendSpot createSpot() { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendSpot entrySpot() { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendActorRef createActor(String actorId, Message createRequest) { throw new UnsupportedOperationException(); }
        @Override public ZLinkBackendActorRef actorLookup(String actorId) { throw new UnsupportedOperationException(); }
        @Override public CompletionStage<ZLinkBackendActorJoinResult> joinActor(ZLinkBackendActorRef actor, RoutingId targetNodeRid, RoutingId targetSpotRid, List<Message> parts, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public CompletionStage<ZLinkBackendActorJoinEntrySpotResult> joinActorEntrySpot(ZLinkBackendActorRef actor, RoutingId targetNodeRid, Message request, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public CompletionStage<List<Message>> leaveActor(ZLinkBackendActorRef actor, RoutingId currentSpotRid, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public CompletionStage<Void> destroyActor(ZLinkBackendActorRef actor, Duration timeout) { throw new UnsupportedOperationException(); }
        @Override public boolean sendActorBoundSession(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) { throw new UnsupportedOperationException(); }
        @Override public void replyActorNoBind(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid, long requestId, int flags, List<Message> parts) { throw new UnsupportedOperationException(); }

        @Override
        public boolean sendToActor(ZLinkBackendActorRef actor, List<Message> parts, SendFlags flags) {
            sendAttempts++;
            if (sendFailure != null) {
                throw new ZlinkSubmitException(sendFailure);
            }
            sentActor = actor;
            return true;
        }

        @Override
        public CompletionStage<List<Message>> requestToActor(
            ZLinkBackendActorRef actor,
            List<Message> parts,
            SendFlags flags,
            Duration timeout) {
            requestedActor = actor;
            return java.util.concurrent.CompletableFuture.completedFuture(
                reply.stream().map(Message::from).toList());
        }

        @Override public boolean forwardActorBoundSession(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid, List<Message> parts, SendFlags flags) { throw new UnsupportedOperationException(); }
        @Override public void bindRemoteActorBoundSession(ZLinkBackendActorRef actor, RoutingId sourceNodeRid, RoutingId sourceSessionRid) { }
        @Override public void closeActorBoundSession(ZLinkBackendActorRef actor, Duration timeout) { }
        @Override public String name() { return "recording"; }
        @Override public void close() { }
    }

    private static final class RequestFailingSpotNode extends RecordingSpotNode {
        private final RequestResult result;

        RequestFailingSpotNode(RequestResult result) {
            this.result = result;
        }

        @Override
        public CompletionStage<List<Message>> requestToActor(
            ZLinkBackendActorRef actor,
            List<Message> parts,
            SendFlags flags,
            Duration timeout) {
            return java.util.concurrent.CompletableFuture.failedFuture(new ZlinkRequestException(result));
        }
    }
}
