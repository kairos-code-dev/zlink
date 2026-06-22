package systems.zlink.samples.kotlin.gamequest.server.questmission.application

import org.springframework.stereotype.Component
import systems.zlink.samples.kotlin.gamequest.server.questmission.QuestMissionOptions
import systems.zlink.samples.kotlin.gamequest.server.questmission.domain.QuestDomain
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventEnvelope
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.StoredQuestEvent
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressRes

/**
 * Owns the event-sourced quest workflow: it dedupes the source gameplay event,
 * applies the projection transition, appends quest events, and notifies the
 * bound GameApi over its channel. Mirrors the .NET `QuestEventProcessor`.
 */
@Component
class QuestEventProcessor(
    private val store: QuestProgressStore,
    private val playerQuestOwners: PlayerQuestOwnerProvisioner,
    private val ownerRouter: QuestOwnerRouter,
    private val snapshots: GameApiSnapshotClient,
    private val notifications: QuestProgressNotifier,
    options: QuestMissionOptions,
) {
    private val missionName = options.missionName

    fun process(gameplayEvent: GameplayEventEnvelope) {
        if (!ownerRouter.isLocalOwner(gameplayEvent.playerId)) {
            return
        }

        val definition = if (gameplayEvent.eventType == "SnapshotKillCount") {
            QuestDomain.firstHunt()
        } else {
            QuestDomain.match(gameplayEvent)
        } ?: return

        playerQuestOwners.ensure(gameplayEvent.playerId)
        val before = store.readProgress(gameplayEvent.playerId, definition.questId)
        if (store.hasSourceEvent(gameplayEvent.playerId, definition.questId, gameplayEvent.eventId)) {
            return
        }

        val after = QuestDomain.apply(definition, before, gameplayEvent)
        val questEvents = QuestDomain.toEvents(definition, before, after, gameplayEvent)
        if (questEvents.isEmpty()) {
            return
        }
        if (!store.appendAndProject(after, questEvents)) {
            return
        }

        val projection = store.readProjection(gameplayEvent.playerId)
        val completed = questEvents.any { it.eventType == "QuestCompletedEvent" }
        val notified = notifications.notify(
            gameplayEvent.sourceApi,
            NotifyQuestProgressReq(
                gameplayEvent.playerId,
                projection,
                if (completed) definition.questId else null,
            ),
        )

        System.err.printf(
            "gamequest mission processed mission=%s player=%s quest=%s source=%s events=%d notified=%s%n",
            missionName, gameplayEvent.playerId, definition.questId, gameplayEvent.eventId,
            questEvents.size, notified,
        )
    }

    fun sync(request: SyncQuestProgressReq): SyncQuestProgressRes {
        if (!ownerRouter.isLocalOwner(request.playerId)) {
            return SyncQuestProgressRes(store.readProjection(request.playerId))
        }

        val snapshot: GetGameplaySnapshotRes = snapshots.read("api-a", request.playerId)
        val killCount = snapshot.killCounts.sumOf { it.count }
        if (killCount > 0) {
            process(
                GameplayEventEnvelope(
                    "${request.playerId}-snapshot-${snapshot.snapshotVersion}",
                    request.playerId,
                    "snapshot-${snapshot.snapshotVersion}",
                    "SnapshotKillCount",
                    "kills",
                    killCount,
                    "api-a",
                    System.currentTimeMillis(),
                ),
            )
        }
        return SyncQuestProgressRes(store.readProjection(request.playerId))
    }

    interface QuestProgressStore {
        fun readProgress(playerId: String, questId: String): QuestProgress?

        fun readProjection(playerId: String): List<QuestProgress>

        fun hasSourceEvent(playerId: String, questId: String, sourceEventId: String): Boolean

        fun appendAndProject(progress: QuestProgress, events: List<StoredQuestEvent>): Boolean
    }

    interface PlayerQuestOwnerProvisioner {
        fun ensure(playerId: String)
    }

    interface GameApiSnapshotClient {
        fun read(apiName: String, playerId: String): GetGameplaySnapshotRes
    }

    interface QuestProgressNotifier {
        fun notify(sourceApi: String, request: NotifyQuestProgressReq): Boolean
    }
}
