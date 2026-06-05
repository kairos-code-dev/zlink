package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.framework.streams.ZLinkSessionContext;

public final class PlayerSessionDirectory {
    private final Map<String, ZLinkSessionContext> sessions = new ConcurrentHashMap<>();

    public void bind(String actorId, ZLinkSessionContext context) {
        sessions.put(actorId, context);
    }

    public void unbind(String actorId, ZLinkSessionContext context) {
        sessions.computeIfPresent(actorId, (ignored, existing) -> existing == context ? null : existing);
    }

    public Optional<ZLinkSessionContext> find(String actorId) {
        return Optional.ofNullable(sessions.get(actorId));
    }
}
