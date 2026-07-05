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
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.SpotNodePeerEntry;
import systems.zlink.contracts.service.spot.SpotNodeStatus;
import systems.zlink.contracts.service.spot.SpotNodeSubjectEntry;
import systems.zlink.contracts.sockets.SendFlags;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.locations.ZLinkActorLocation;
import systems.zlink.framework.locations.ZLinkLocationWriteIntent;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinEntrySpotResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorJoinResult;
import systems.zlink.framework.runtime.backend.ZLinkBackendActorRef;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpotNode;
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
    void sendAndRequestResolveActorIdAndUseBackendNoBindOperations() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        store.renewOwnerLeaseAsync("owner", RoutingId.from("actor-node"), Duration.ofMinutes(1))
            .toCompletableFuture()
            .join();
        store.updateActorAsync(
                new ZLinkActorLocation(
                    "actor-1",
                    "test",
                    new ZLinkActorRef(RoutingId.from("actor-node"), "actor-1", 7),
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

        client.sendToActor("actor-1", new Ping("hello"))
            .packetName("Ping")
            .submit()
            .toCompletableFuture()
            .join();
        Pong pong = client.requestToActor("actor-1", new Ping("hello"))
            .packetName("Ping")
            .submit(Pong.class)
            .toCompletableFuture()
            .join();

        assertEquals("actor-1", node.sentActor.actorId());
        assertEquals(7, node.sentActor.generation());
        assertEquals("actor-1", node.requestedActor.actorId());
        assertEquals("pong", pong.value());
    }

    @Test
    void missingActorFailsAsActorRouteNotFound() {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            RecordingSpotNode::new,
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(store),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofSeconds(5));

        CompletionException error = assertThrows(
            CompletionException.class,
            () -> client.sendToActor("missing", new Ping("hello"))
                .submit()
                .toCompletableFuture()
                .join());

        ZLinkFrameworkException frameworkError = (ZLinkFrameworkException) error.getCause();
        assertEquals(ZLinkFrameworkErrorKind.ACTOR_ROUTE_NOT_FOUND, frameworkError.kind());
    }

    @Test
    void noBindActorSendRetriesWhileRouteConverges() {
        ZLinkInMemoryLocationStore store = storeWithActor("actor-1");
        RecordingSpotNode node = new RecordingSpotNode();
        node.sendFailuresRemaining = 1;
        ZLinkActorClientRuntime client = new ZLinkActorClientRuntime(
            () -> node,
            new ZLinkStoreLocationResolvers(
                ZLinkRegisteredLocationStores.fromUnified(store),
                new systems.zlink.framework.locations.ZLinkLocationOptions()),
            new ZLinkJsonMessageSerializer(),
            Duration.ofMillis(250));

        client.sendToActor("actor-1", new Ping("hello"))
            .packetName("Ping")
            .submit()
            .toCompletableFuture()
            .join();

        assertEquals(2, node.sendAttempts);
        assertEquals("actor-1", node.sentActor.actorId());
    }

    private static ZLinkInMemoryLocationStore storeWithActor(String actorId) {
        ZLinkInMemoryLocationStore store = new ZLinkInMemoryLocationStore();
        store.renewOwnerLeaseAsync("owner", RoutingId.from("actor-node"), Duration.ofMinutes(1))
            .toCompletableFuture()
            .join();
        store.updateActorAsync(
                new ZLinkActorLocation(
                    actorId,
                    "test",
                    new ZLinkActorRef(RoutingId.from("actor-node"), actorId, 7),
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

    private static final class RecordingSpotNode implements ZLinkBackendSpotNode {
        private final List<Message> reply;
        ZLinkBackendActorRef sentActor;
        ZLinkBackendActorRef requestedActor;
        int sendAttempts;
        int sendFailuresRemaining;

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
            if (sendFailuresRemaining > 0) {
                sendFailuresRemaining--;
                throw new IllegalStateException("NOT_CONNECTED");
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
        @Override public SpotNodeStatus status() { throw new UnsupportedOperationException(); }
        @Override public List<SpotNodePeerEntry> peers() { return List.of(); }
        @Override public List<SpotNodeSubjectEntry> subjects() { return List.of(); }
        @Override public String name() { return "recording"; }
        @Override public void close() { }
    }
}
