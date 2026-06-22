package systems.zlink.samples.kotlin.gamequest.server.questmission.application

import com.fasterxml.jackson.databind.ObjectMapper
import java.nio.charset.StandardCharsets
import org.springframework.stereotype.Component
import systems.zlink.contracts.core.RoutingId
import systems.zlink.contracts.messaging.Message
import systems.zlink.framework.ZLinkAwait
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.spots.ZLinkSpotManager
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.questmission.QuestMissionOptions
import systems.zlink.samples.kotlin.gamequest.server.questmission.domain.QuestDomain
import systems.zlink.samples.kotlin.gamequest.server.questmission.spots.PlayerQuestSpot
import systems.zlink.samples.kotlin.gamequest.server.questmission.store.QuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventEnvelope
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.NotifyQuestProgressRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressRes

/**
 * Owns the event-sourced quest workflow: it dedupes the source gameplay event,
 * applies the projection transition, appends quest events, and notifies the
 * bound GameApi over its channel. Mirrors the .NET `QuestEventProcessor`.
 */
@Component
class QuestEventProcessor(
    private val store: QuestStore,
    private val spots: ZLinkSpotManager,
    private val ownerRouter: QuestOwnerRouter,
    private val channels: ZLinkClient,
    private val json: ObjectMapper,
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

        ensurePlayerQuestSpot(gameplayEvent.playerId)
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
        val notified = notifyBoundGameApi(
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

        val snapshot: GetGameplaySnapshotRes = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel("api-a"),
                GetGameplaySnapshotReq(request.playerId),
            )
            .timeout(SampleNames.RequestTimeout)
            .await(GetGameplaySnapshotRes::class.java)
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

    private fun ensurePlayerQuestSpot(playerId: String) {
        val payload = Message.from(encode(PlayerQuestSpot.PlayerQuestSpotCreateReq(playerId)))
        try {
            ZLinkAwait.await(
                spots.getOrCreate(
                    PlayerQuestSpot::class.java,
                    RoutingId.from("player:$playerId".toByteArray(StandardCharsets.UTF_8)),
                    payload,
                ),
            )
        } finally {
            payload.close()
        }
    }

    private fun notifyBoundGameApi(sourceApi: String, request: NotifyQuestProgressReq): Boolean {
        val apiName = if (sourceApi == "api-b") "api-b" else "api-a"
        return try {
            val delivered = channels
                .requestToChannel(SampleNames.gameApiActionChannel(apiName), request)
                .timeout(SampleNames.RequestTimeout)
                .await(NotifyQuestProgressRes::class.java)
            delivered != null && delivered.delivered
        } catch (error: RuntimeException) {
            System.err.printf(
                "gamequest mission projection kept while stream notify failed. player=%s%n",
                request.playerId,
            )
            false
        }
    }

    private fun encode(value: Any): ByteArray = json.writeValueAsBytes(value)
}
