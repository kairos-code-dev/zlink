package systems.zlink.samples.gamequest.server.gameapi.sessions;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

public final class GameQuestSessionRegistry {
    private final ConcurrentMap<String, GameQuestSession> sessions = new ConcurrentHashMap<>();

    public void bind(String playerId, GameQuestSession session) {
        sessions.put(playerId, session);
    }

    public void unbind(String playerId, GameQuestSession session) {
        sessions.remove(playerId, session);
    }

    public CompletionStage<Void> deliver(Messages.QuestProcessingMsg message) {
        GameQuestSession session = sessions.get(message.playerId());
        return session == null
            ? CompletableFuture.completedFuture(null)
            : session.deliver(message);
    }
}
