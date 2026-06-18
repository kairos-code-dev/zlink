package systems.zlink.samples.gamequest.server.gameapi.session;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import org.springframework.stereotype.Component;
import systems.zlink.framework.streams.ZLinkSessionContext;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.server.gameapi.GameApiInstanceOptions;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Tracks the live stream session per player and pushes quest notifications.
 * Mirrors the .NET {@code GameQuestSessionRegistry}.
 */
@Component
public final class GameQuestSessionRegistry {
    private final Object gate = new Object();
    private final Map<String, ZLinkSessionContext> sessionsByPlayer = new HashMap<>();
    private final String apiName;

    public GameQuestSessionRegistry(GameApiInstanceOptions options) {
        this.apiName = options.apiName();
    }

    public Messages.BindQuestSessionReq bind(String playerId, ZLinkSessionContext context) {
        Messages.BindQuestSessionReq binding = new Messages.BindQuestSessionReq(
            playerId, context.sessionId(), apiName);
        synchronized (gate) {
            sessionsByPlayer.put(playerId, context);
        }
        return binding;
    }

    public List<Messages.UnbindQuestSessionReq> remove(ZLinkSessionContext context) {
        List<Messages.UnbindQuestSessionReq> removed = new ArrayList<>();
        synchronized (gate) {
            for (Map.Entry<String, ZLinkSessionContext> entry
                : new ArrayList<>(sessionsByPlayer.entrySet())) {
                if (entry.getValue() == context) {
                    sessionsByPlayer.remove(entry.getKey());
                    removed.add(new Messages.UnbindQuestSessionReq(entry.getKey(), context.sessionId()));
                }
            }
        }
        return removed;
    }

    public boolean notify(Messages.NotifyQuestProgressReq request) {
        ZLinkSessionContext session;
        synchronized (gate) {
            session = sessionsByPlayer.get(request.playerId());
        }
        if (session == null) {
            return false;
        }

        try {
            for (Messages.QuestProgress progress : request.projection()) {
                session.client()
                    .send(new Messages.QuestProgressNotify(request.playerId(), session.sessionId(), progress))
                    .packetName(SampleNames.ProgressPacket)
                    .await();
            }
            if (request.completedQuestId() != null && !request.completedQuestId().isBlank()) {
                Messages.QuestProgress completed = request.projection().stream()
                    .filter(progress -> progress.questId().equals(request.completedQuestId()))
                    .findFirst()
                    .orElseThrow(() -> new IllegalStateException(
                        "Completed quest projection entry missing for " + request.completedQuestId() + "."));
                session.client()
                    .send(new Messages.QuestCompletedNotify(
                        request.playerId(), session.sessionId(), completed, true))
                    .packetName(SampleNames.CompletedPacket)
                    .await();
            }
        } catch (RuntimeException error) {
            remove(session);
            System.err.printf(
                "gamequest stream notify skipped because the session is gone. player=%s session=%s%n",
                request.playerId(), session.sessionId());
            return false;
        }
        return true;
    }
}
