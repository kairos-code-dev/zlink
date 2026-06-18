package systems.zlink.samples.gamequest.server.gameapi.handlers;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.gamequest.server.configuration.QuestIds;
import systems.zlink.samples.gamequest.server.configuration.QuestStatuses;
import systems.zlink.samples.gamequest.server.gameapi.contracts.SelfCheckMessages;
import systems.zlink.samples.gamequest.server.gameapi.store.GameQuestStore;
import systems.zlink.samples.gamequest.shared.contracts.Messages;

/**
 * Server-side assertion over the event-sourced evidence. Mirrors the .NET
 * {@code /self-check/assert} endpoint: it verifies reward-granted projections,
 * binding history, and the per-quest stored event counts.
 */
@ZLinkHandlerGroup("gameapi")
public final class GameQuestServerAssertHandler
    implements ZLinkRequestHandler<
        SelfCheckMessages.GameQuestServerAssertReq,
        Messages.GameQuestServerAssertRes> {
    private final GameQuestStore store;

    public GameQuestServerAssertHandler(GameQuestStore store) {
        this.store = store;
    }

    @Override
    public Messages.GameQuestServerAssertRes handle(
        SelfCheckMessages.GameQuestServerAssertReq request, ZLinkRequestContext context) {
        List<Messages.QuestProgress> alice = store.readProjection("player-alice");
        List<Messages.QuestProgress> bob = store.readProjection("player-bob");
        List<Messages.BindQuestSessionReq> bindings = store.readBindingHistory();
        List<Messages.BindQuestSessionReq> activeBindings = store.readBindings();
        List<Messages.StoredQuestEvent> events = store.readQuestEvents();

        boolean passed =
            hasStatus(alice, QuestIds.FirstHunt, QuestStatuses.RewardGranted)
            && hasStatus(alice, QuestIds.OpenAuction, QuestStatuses.RewardGranted)
            && hasStatus(bob, QuestIds.HerbGathering, QuestStatuses.RewardGranted)
            && bindings.stream().anyMatch(binding ->
                binding.playerId().equals("player-bob")
                    && "api-b".equals(binding.gameApiInstanceId()))
            && activeBindings.stream().noneMatch(binding -> binding.playerId().equals("player-alice"))
            && count(events, "player-alice", QuestIds.FirstHunt, "QuestProgressedEvent") == 3
            && count(events, "player-alice", QuestIds.FirstHunt, "QuestCompletedEvent") == 1
            && count(events, "player-alice", QuestIds.FirstHunt, "QuestRewardGrantedEvent") == 1
            && count(events, "player-alice", QuestIds.FirstHunt, "QuestProgressReconciledEvent") == 1
            && count(events, "player-alice", QuestIds.OpenAuction, "QuestCompletedEvent") == 1
            && count(events, "player-alice", QuestIds.OpenAuction, "QuestRewardGrantedEvent") == 1
            && count(events, "player-bob", QuestIds.HerbGathering, "QuestCompletedEvent") == 1
            && count(events, "player-bob", QuestIds.HerbGathering, "QuestRewardGrantedEvent") == 1
            && uniqueVersions(events);

        TreeSet<String> evidence = new TreeSet<>();
        for (Messages.QuestProgress progress : alice) {
            evidence.add(progressLine(progress));
        }
        for (Messages.QuestProgress progress : bob) {
            evidence.add(progressLine(progress));
        }
        for (Messages.BindQuestSessionReq binding : bindings) {
            evidence.add("binding:" + binding.playerId() + ":" + binding.connectionId()
                + ":" + binding.gameApiInstanceId());
        }
        for (Messages.BindQuestSessionReq binding : activeBindings) {
            evidence.add("active-binding:" + binding.playerId() + ":" + binding.connectionId()
                + ":" + binding.gameApiInstanceId());
        }
        for (Messages.StoredQuestEvent event : events) {
            evidence.add("event:" + event.playerId() + ":" + event.questId() + ":" + event.eventType()
                + ":v" + event.version() + ":source=" + event.sourceEventId());
        }

        System.err.printf("gamequest evidence: passed=%s%n", passed);
        return new Messages.GameQuestServerAssertRes(passed, new ArrayList<>(evidence));
    }

    private static String progressLine(Messages.QuestProgress progress) {
        return progress.playerId() + ":" + progress.questId() + ":" + progress.status()
            + ":" + progress.currentCount() + "/" + progress.requiredCount();
    }

    private static boolean hasStatus(List<Messages.QuestProgress> projection, String questId, String status) {
        return projection.stream().anyMatch(progress ->
            progress.questId().equals(questId) && progress.status().equals(status));
    }

    private static int count(
        List<Messages.StoredQuestEvent> events, String playerId, String questId, String eventType) {
        int total = 0;
        for (Messages.StoredQuestEvent event : events) {
            if (event.playerId().equals(playerId)
                && event.questId().equals(questId)
                && event.eventType().equals(eventType)) {
                total++;
            }
        }
        return total;
    }

    private static boolean uniqueVersions(List<Messages.StoredQuestEvent> events) {
        Set<String> seen = new HashSet<>();
        for (Messages.StoredQuestEvent event : events) {
            if (!seen.add(event.playerId() + "|" + event.questId() + "|" + event.version())) {
                return false;
            }
        }
        return true;
    }
}
