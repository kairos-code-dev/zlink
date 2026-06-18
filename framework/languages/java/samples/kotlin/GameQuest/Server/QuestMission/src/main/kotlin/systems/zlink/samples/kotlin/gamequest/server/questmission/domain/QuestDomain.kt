package systems.zlink.samples.kotlin.gamequest.server.questmission.domain

import com.fasterxml.jackson.databind.ObjectMapper
import com.fasterxml.jackson.databind.json.JsonMapper
import com.fasterxml.jackson.module.kotlin.registerKotlinModule
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestIds
import systems.zlink.samples.kotlin.gamequest.server.configuration.QuestStatuses
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventEnvelope
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestCompletedEvent
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgressReconciledEvent
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgressedEvent
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestRewardGrantedEvent
import systems.zlink.samples.kotlin.gamequest.shared.contracts.StoredQuestEvent

/** Quest catalog and projection builder. Mirrors the .NET `QuestDomain`. */
object QuestDomain {
    private val json: ObjectMapper = JsonMapper.builder().build().registerKotlinModule()

    data class QuestDefinition(val questId: String, val eventType: String, val value: String, val required: Int)

    val CATALOG: List<QuestDefinition> = listOf(
        QuestDefinition(QuestIds.FirstHunt, "MonsterKilled", "*", 3),
        QuestDefinition(QuestIds.OpenAuction, "FeatureUnlocked", "auction", 1),
        QuestDefinition(QuestIds.HerbGathering, "ItemCollected", "healing-herb", 5),
        QuestDefinition(QuestIds.ClearTutorial, "MissionCompleted", "tutorial", 1),
        QuestDefinition(QuestIds.VisitRuins, "AreaEntered", "ruins", 1),
    )

    fun match(event: GameplayEventEnvelope): QuestDefinition? =
        CATALOG.firstOrNull {
            it.eventType == event.eventType && (it.value == "*" || it.value == event.value)
        }

    fun firstHunt(): QuestDefinition =
        CATALOG.first { it.questId == QuestIds.FirstHunt }

    fun apply(
        definition: QuestDefinition,
        current: QuestProgress?,
        event: GameplayEventEnvelope,
    ): QuestProgress {
        val currentCount = current?.currentCount ?: 0
        val nextCount = if (event.eventType == "SnapshotKillCount") {
            maxOf(currentCount, event.count)
        } else {
            minOf(definition.required, currentCount + event.count)
        }
        val complete = nextCount >= definition.required
        return QuestProgress(
            event.playerId,
            definition.questId,
            if (complete) QuestStatuses.RewardGranted else QuestStatuses.Active,
            nextCount,
            definition.required,
            event.eventId,
            System.currentTimeMillis(),
        )
    }

    fun toEvents(
        definition: QuestDefinition,
        before: QuestProgress?,
        after: QuestProgress,
        source: GameplayEventEnvelope,
    ): List<StoredQuestEvent> {
        if (before != null && source.eventId == before.lastEventId) {
            return emptyList()
        }
        if (before != null && before.currentCount == after.currentCount && before.status == after.status) {
            return emptyList()
        }

        val events = mutableListOf<StoredQuestEvent>()
        val kind = if (source.eventType == "SnapshotKillCount") {
            "QuestProgressReconciledEvent"
        } else {
            "QuestProgressedEvent"
        }
        events.add(create(kind, definition, before, after, source))

        val beforeStatus = before?.status
        if (beforeStatus != QuestStatuses.RewardGranted && after.status == QuestStatuses.RewardGranted) {
            events.add(create("QuestCompletedEvent", definition, before, after, source))
            events.add(create("QuestRewardGrantedEvent", definition, before, after, source))
        }
        return events
    }

    private fun create(
        eventType: String,
        definition: QuestDefinition,
        before: QuestProgress?,
        after: QuestProgress,
        source: GameplayEventEnvelope,
    ): StoredQuestEvent {
        val now = System.currentTimeMillis()
        val eventId = "${after.playerId}-${definition.questId}-${source.eventId}-$eventType"
        val payload: Any = when (eventType) {
            "QuestProgressedEvent" -> QuestProgressedEvent(
                eventId,
                after.playerId,
                definition.questId,
                maxOf(0, after.currentCount - (before?.currentCount ?: 0)),
                after.currentCount,
                after.requiredCount,
                source.eventId,
            )
            "QuestCompletedEvent" -> QuestCompletedEvent(
                eventId, after.playerId, definition.questId, source.eventId, now,
            )
            "QuestRewardGrantedEvent" -> QuestRewardGrantedEvent(
                eventId, after.playerId, definition.questId, source.eventId,
                "reward-${definition.questId}", now,
            )
            "QuestProgressReconciledEvent" -> QuestProgressReconciledEvent(
                eventId, after.playerId, definition.questId, after.currentCount, "GameplaySnapshot", now,
            )
            else -> throw IllegalArgumentException("Unknown quest event type: $eventType")
        }
        return StoredQuestEvent(
            eventId,
            source.eventId,
            after.playerId,
            definition.questId,
            eventType,
            json.writeValueAsBytes(payload),
            0,
            now,
        )
    }
}
