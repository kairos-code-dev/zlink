package systems.zlink.samples.tictactoe.sessiongateway.server.session.sessions;

import java.util.Map;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.framework.streams.ZLinkSessionContext;

public final class PlayerSessionDirectory {
    private static final Map<String, ZLinkSessionContext> SESSIONS = new ConcurrentHashMap<>();

    private PlayerSessionDirectory() {
    }

    public static void bind(String actorId, ZLinkSessionContext context) {
        SESSIONS.put(actorId, context);
    }

    public static void unbind(String actorId, ZLinkSessionContext context) {
        SESSIONS.computeIfPresent(actorId, (ignored, existing) -> existing == context ? null : existing);
    }

    public static Optional<ZLinkSessionContext> find(String actorId) {
        return Optional.ofNullable(SESSIONS.get(actorId));
    }
}
