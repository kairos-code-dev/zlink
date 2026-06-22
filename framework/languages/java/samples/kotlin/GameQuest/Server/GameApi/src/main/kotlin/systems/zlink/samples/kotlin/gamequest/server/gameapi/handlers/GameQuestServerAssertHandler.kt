package systems.zlink.samples.kotlin.gamequest.server.gameapi.handlers

import java.util.TreeSet
import systems.zlink.framework.channels.ZLinkRequestContext
import systems.zlink.framework.kotlin.ZLinkSuspendingRequestHandler
import systems.zlink.framework.handlers.ZLinkHandlerGroup
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestIds
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestStatuses
import systems.zlink.samples.kotlin.gamequest.server.gameapi.contracts.GameQuestServerAssertReq
import systems.zlink.samples.kotlin.gamequest.server.gameapi.infrastructure.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameQuestServerAssertRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.StoredQuestEvent

/**
 * Server-side assertion over the event-sourced evidence. Mirrors the .NET
 * `/self-check/assert` endpoint: it verifies reward-granted projections, binding
 * history, and the per-quest stored event counts.
 */
@ZLinkHandlerGroup("gameapi")
class GameQuestServerAssertHandler(
    private val store: GameQuestStore,
) : ZLinkSuspendingRequestHandler<GameQuestServerAssertReq, GameQuestServerAssertRes> {
    override suspend fun handle(request: GameQuestServerAssertReq, context: ZLinkRequestContext): GameQuestServerAssertRes {
        val alice = store.readProjection("player-alice")
        val bob = store.readProjection("player-bob")
        val bindings = store.readBindingHistory()
        val activeBindings = store.readBindings()
        val events = store.readQuestEvents()

        val passed =
            hasStatus(alice, QuestIds.FirstHunt, QuestStatuses.RewardGranted) &&
                hasStatus(alice, QuestIds.OpenAuction, QuestStatuses.RewardGranted) &&
                hasStatus(bob, QuestIds.HerbGathering, QuestStatuses.RewardGranted) &&
                bindings.any { it.playerId == "player-bob" && it.gameApiInstanceId == "api-b" } &&
                activeBindings.none { it.playerId == "player-alice" } &&
                count(events, "player-alice", QuestIds.FirstHunt, "QuestProgressedEvent") == 3 &&
                count(events, "player-alice", QuestIds.FirstHunt, "QuestCompletedEvent") == 1 &&
                count(events, "player-alice", QuestIds.FirstHunt, "QuestRewardGrantedEvent") == 1 &&
                count(events, "player-alice", QuestIds.FirstHunt, "QuestProgressReconciledEvent") == 1 &&
                count(events, "player-alice", QuestIds.OpenAuction, "QuestCompletedEvent") == 1 &&
                count(events, "player-alice", QuestIds.OpenAuction, "QuestRewardGrantedEvent") == 1 &&
                count(events, "player-bob", QuestIds.HerbGathering, "QuestCompletedEvent") == 1 &&
                count(events, "player-bob", QuestIds.HerbGathering, "QuestRewardGrantedEvent") == 1 &&
                uniqueVersions(events)

        val evidence = TreeSet<String>()
        for (progress in alice) {
            evidence.add(progressLine(progress))
        }
        for (progress in bob) {
            evidence.add(progressLine(progress))
        }
        for (binding in bindings) {
            evidence.add("binding:${binding.playerId}:${binding.connectionId}:${binding.gameApiInstanceId}")
        }
        for (event in events) {
            evidence.add(
                "event:${event.playerId}:${event.questId}:${event.eventType}" +
                    ":v${event.version}:source=${event.sourceEventId}",
            )
        }

        System.err.printf("gamequest evidence: passed=%s%n", passed)
        return GameQuestServerAssertRes(passed, evidence.toList())
    }

    private fun progressLine(progress: QuestProgress): String =
        "${progress.playerId}:${progress.questId}:${progress.status}" +
            ":${progress.currentCount}/${progress.requiredCount}"

    private fun hasStatus(projection: List<QuestProgress>, questId: String, status: String): Boolean =
        projection.any { it.questId == questId && it.status == status }

    private fun count(
        events: List<StoredQuestEvent>,
        playerId: String,
        questId: String,
        eventType: String,
    ): Int = events.count { it.playerId == playerId && it.questId == questId && it.eventType == eventType }

    private fun uniqueVersions(events: List<StoredQuestEvent>): Boolean {
        val seen = HashSet<String>()
        for (event in events) {
            if (!seen.add("${event.playerId}|${event.questId}|${event.version}")) {
                return false
            }
        }
        return true
    }
}
