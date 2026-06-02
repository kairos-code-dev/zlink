package systems.zlink.samples.tictactoe.sessiongateway;

import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorContext;
import systems.zlink.framework.actors.ZLinkActorRef;
import systems.zlink.framework.actors.ZLinkBoundSession;
import systems.zlink.framework.actors.ZLinkBoundSessionSendCall;
import systems.zlink.framework.configuration.ZLinkStreamNodeBuilder;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionActor;
import systems.zlink.framework.streams.ZLinkSessionActors;
import systems.zlink.framework.streams.ZLinkSessionClient;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.framework.streams.ZLinkSessionReplyCall;
import systems.zlink.framework.streams.ZLinkSessionSendCall;
import systems.zlink.framework.streams.ZLinkStreamError;
import systems.zlink.framework.streams.ZLinkStreamHeader;

public final class TicTacToeSessionGatewaySample {
    public static void main(String[] args) {
        RecordingStreamNodeBuilder streamNode = new RecordingStreamNodeBuilder();
        streamNode.bind("tcp://127.0.0.1:29010");
        streamNode.attachActorGateway("session-relay");
        streamNode.registerSession(PlayerSession.class);
        require(streamNode.actorGatewayAttached(), "stream node did not attach ActorGateway");

        ZLinkActorRef actorRef = new ZLinkActorRef(RoutingId.from("play-node"), "player-1", 1);
        RecordingSessionActors primaryActors = new RecordingSessionActors();
        PlayerSession primary = new PlayerSession(primaryActors);
        primary.bindActor(actorRef).toCompletableFuture().join();

        RecordingSessionActors reconnectActors = new RecordingSessionActors();
        PlayerSession reconnect = new PlayerSession(reconnectActors);
        reconnect.bindActor(actorRef).toCompletableFuture().join();

        require(primaryActors.bound().get(0).actorId().equals("player-1"),
            "primary session did not bind expected actor");
        require(reconnectActors.bound().get(0).actorId().equals("player-1"),
            "reconnect session did not keep actor id");

        PlayerActor actor = new PlayerActor("player-1");
        actor.context().boundSession()
            .send("GameState")
            .packetName("GameStateChanged")
            .submitAsync()
            .toCompletableFuture()
            .join();
        require(actor.pushes().contains("GameStateChanged:GameState"),
            "boundSession push did not reach client-facing session");
        System.out.println("TicTacToe.SessionGateway sample self-check passed");
    }

    private static void require(boolean condition, String message) {
        if (!condition) {
            throw new IllegalStateException(message);
        }
    }

    public static final class PlayerSession implements ZLinkSession {
        private final RecordingSessionActors actors;

        PlayerSession(RecordingSessionActors actors) {
            this.actors = actors;
        }

        CompletionStage<ZLinkSessionActor> bindActor(ZLinkActorRef actorRef) {
            return actors.bindAsync(actorRef);
        }

        @Override
        public ZLinkSessionContext context() {
            return new RecordingSessionContext(actors);
        }

        @Override
        public CompletionStage<Void> onConnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> onErrorAsync(ZLinkStreamError error) {
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class RecordingSessionActors implements ZLinkSessionActors {
        private final List<ZLinkSessionActor> bound = new ArrayList<>();

        @Override
        public List<ZLinkSessionActor> bound() {
            return List.copyOf(bound);
        }

        @Override
        public CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActor actor) {
            return bindAsync(new ZLinkActorRef(RoutingId.from("local-node"), actor.actorId(), 1));
        }

        @Override
        public CompletionStage<ZLinkSessionActor> bindAsync(ZLinkActorRef actor) {
            ZLinkSessionActor sessionActor = new BoundActor(actor);
            bound.add(sessionActor);
            return CompletableFuture.completedFuture(sessionActor);
        }

        @Override
        public Optional<ZLinkSessionActor> find(String actorId) {
            return bound.stream()
                .filter(actor -> actor.actorId().equals(actorId))
                .findFirst();
        }
    }

    private record BoundActor(ZLinkActorRef ref) implements ZLinkSessionActor {
        @Override
        public String actorId() {
            return ref.actorId();
        }

        @Override
        public CompletionStage<Void> relayAsync(ZLinkStreamHeader header, Message payload) {
            return CompletableFuture.completedFuture(null);
        }

        @Override
        public CompletionStage<Void> notifyDisconnectedAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class PlayerActor implements ZLinkActor {
        private final String actorId;
        private final RecordingActorContext context = new RecordingActorContext();

        PlayerActor(String actorId) {
            this.actorId = actorId;
        }

        @Override
        public String actorId() {
            return actorId;
        }

        @Override
        public ZLinkActorContext context() {
            return context;
        }

        List<String> pushes() {
            return context.pushes();
        }
    }

    private static final class RecordingActorContext implements ZLinkActorContext {
        private final List<String> pushes = new ArrayList<>();

        @Override
        public Optional<RoutingId> spotRid() {
            return Optional.of(RoutingId.from("room-spot"));
        }

        @Override
        public boolean isJoined() {
            return true;
        }

        @Override
        public ZLinkBoundSession boundSession() {
            return new RecordingBoundSession(pushes);
        }

        @Override
        public ZLinkSpot getSpot() {
            throw new UnsupportedOperationException("not needed by sample");
        }

        @Override
        public <TSpot extends ZLinkSpot> TSpot getSpot(Class<TSpot> spotType) {
            throw new UnsupportedOperationException("not needed by sample");
        }

        List<String> pushes() {
            return List.copyOf(pushes);
        }
    }

    private record RecordingBoundSession(List<String> pushes) implements ZLinkBoundSession {
        @Override
        public <TMessage> ZLinkBoundSessionSendCall send(TMessage message) {
            return new RecordingBoundSessionSendCall(pushes, String.valueOf(message));
        }

        @Override
        public CompletionStage<Void> disconnectAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }

    private record RecordingBoundSessionSendCall(
        List<String> pushes,
        String message,
        String packetName) implements ZLinkBoundSessionSendCall {
        RecordingBoundSessionSendCall(List<String> pushes, String message) {
            this(pushes, message, message);
        }

        @Override
        public ZLinkBoundSessionSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkBoundSessionSendCall packetName(String packetName) {
            return new RecordingBoundSessionSendCall(pushes, message, packetName);
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            pushes.add(packetName + ":" + message);
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class RecordingStreamNodeBuilder implements ZLinkStreamNodeBuilder {
        private boolean actorGatewayAttached;

        @Override
        public void bind(String endpoint) {
            require(endpoint.startsWith("tcp://"), "session endpoint must be tcp");
        }

        @Override
        public void attachActorGateway(String spotNodeName) {
            actorGatewayAttached = "session-relay".equals(spotNodeName);
        }

        @Override
        public void registerSession(Class<? extends ZLinkSession> sessionType) {
            require(sessionType == PlayerSession.class, "unexpected session type");
        }

        boolean actorGatewayAttached() {
            return actorGatewayAttached;
        }
    }

    private record RecordingSessionContext(
        RecordingSessionActors actors) implements ZLinkSessionContext {
        @Override
        public String sessionId() {
            return "session-1";
        }

        @Override
        public Optional<RoutingId> routingId() {
            return Optional.of(RoutingId.from("session-rid"));
        }

        @Override
        public Optional<String> localAddr() {
            return Optional.of("tcp://127.0.0.1:29010");
        }

        @Override
        public Optional<String> remoteAddr() {
            return Optional.of("tcp://127.0.0.1:39010");
        }

        @Override
        public ZLinkSessionClient client() {
            return new RecordingSessionClient();
        }

        @Override
        public CompletionStage<Void> closeAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class RecordingSessionClient implements ZLinkSessionClient {
        @Override
        public <TMessage> ZLinkSessionSendCall send(TMessage message) {
            return new RecordingSessionSendCall();
        }

        @Override
        public <TMessage> ZLinkSessionReplyCall reply(TMessage message) {
            return new RecordingSessionReplyCall();
        }
    }

    private static final class RecordingSessionSendCall implements ZLinkSessionSendCall {
        @Override
        public ZLinkSessionSendCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkSessionSendCall packetName(String messageName) {
            return this;
        }

        @Override
        public ZLinkSessionSendCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }

    private static final class RecordingSessionReplyCall implements ZLinkSessionReplyCall {
        @Override
        public ZLinkSessionReplyCall metadata(String key, String value) {
            return this;
        }

        @Override
        public ZLinkSessionReplyCall compress() {
            return this;
        }

        @Override
        public CompletionStage<Void> submitAsync() {
            return CompletableFuture.completedFuture(null);
        }
    }
}
