package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.streams.ZLinkSessionClient;
import systems.zlink.framework.streams.ZLinkSessionContext;

public record RecordingSessionContext(RecordingSessionActors actors) implements ZLinkSessionContext {
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
