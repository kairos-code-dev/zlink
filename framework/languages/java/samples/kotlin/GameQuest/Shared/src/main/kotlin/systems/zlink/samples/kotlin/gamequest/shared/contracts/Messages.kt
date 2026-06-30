package systems.zlink.samples.kotlin.gamequest.shared.contracts

import systems.zlink.framework.handlers.ZLinkPacket

/**
 * Wire contracts for the GameQuest sample.
 *
 * The sample mirrors the .NET GameQuest scenario over ZLink. Stateless
 * `GameApi` servers own the action API and WebSocket session surface and publish
 * gameplay events over ZLink fanout. The stateful `QuestMission` server owns
 * `PlayerQuestSpot` aggregates, event-sourced quest state, and the read-model
 * projection used for client notifications.
 */

@ZLinkPacket("KillMonsterReq")
data class KillMonsterReq(
    val playerId: String,
    val monsterId: String,
    val areaId: String,
    val idempotencyKey: String,
)

data class KillMonsterRes(val eventId: String)

@ZLinkPacket("CollectItemReq")
data class CollectItemReq(
    val playerId: String,
    val itemId: String,
    val count: Int,
    val idempotencyKey: String,
)

data class CollectItemRes(val eventId: String)

@ZLinkPacket("CompleteMissionReq")
data class CompleteMissionReq(
    val playerId: String,
    val missionId: String,
    val idempotencyKey: String,
)

data class CompleteMissionRes(val eventId: String)

@ZLinkPacket("EnterAreaReq")
data class EnterAreaReq(
    val playerId: String,
    val areaId: String,
    val idempotencyKey: String,
)

data class EnterAreaRes(val eventId: String)

@ZLinkPacket("UnlockFeatureReq")
data class UnlockFeatureReq(
    val playerId: String,
    val featureId: String,
    val idempotencyKey: String,
)

data class UnlockFeatureRes(val eventId: String)

@ZLinkPacket("SubscribeQuestReq")
data class SubscribeQuestReq(val playerId: String)

data class SubscribeQuestRes(val activeQuests: List<QuestProgress>)

@ZLinkPacket("GetQuestProgressReq")
data class GetQuestProgressReq(val playerId: String)

data class GetQuestProgressRes(val activeQuests: List<QuestProgress>)

@ZLinkPacket("SyncQuestProgressReq")
data class SyncQuestProgressReq(val playerId: String)

data class SyncQuestProgressRes(val updatedQuests: List<QuestProgress>)

@ZLinkPacket("GetGameplaySnapshotReq")
data class GetGameplaySnapshotReq(val playerId: String)

data class GetGameplaySnapshotRes(
    val playerId: String,
    val killCounts: List<KillCountSnapshot>,
    val itemCounts: List<ItemCountSnapshot>,
    val completedMissionIds: List<String>,
    val unlockedFeatureIds: List<String>,
    val enteredAreaIds: List<String>,
    val snapshotVersion: Long,
)

data class KillCountSnapshot(val monsterId: String, val areaId: String?, val count: Int)

data class ItemCountSnapshot(val itemId: String, val count: Int)

@ZLinkPacket("BindQuestSessionReq")
data class BindQuestSessionReq(
    val playerId: String,
    val connectionId: String,
    val gameApiInstanceId: String,
)

data class BindQuestSessionRes(val bound: Boolean)

@ZLinkPacket("UnbindQuestSessionReq")
data class UnbindQuestSessionReq(val playerId: String, val connectionId: String)

data class UnbindQuestSessionRes(val unbound: Boolean)

@ZLinkPacket("QuestProgressNotify")
data class QuestProgressNotify(
    val playerId: String,
    val targetConnectionId: String,
    val progress: QuestProgress,
)

@ZLinkPacket("QuestCompletedNotify")
data class QuestCompletedNotify(
    val playerId: String,
    val targetConnectionId: String,
    val progress: QuestProgress,
    val rewardGranted: Boolean,
)

data class QuestProgress(
    val playerId: String,
    val questId: String,
    val status: String,
    val currentCount: Int,
    val requiredCount: Int,
    val lastEventId: String?,
    val updatedAtUnixMs: Long,
)

@ZLinkPacket("GameplayEventMsg")
data class GameplayEventMsg(
    val eventId: String,
    val playerId: String,
    val idempotencyKey: String,
    val eventType: String,
    val value: String,
    val count: Int,
    val sourceApi: String,
    val createdAtUnixMs: Long,
)

data class QuestProgressedEvent(
    val eventId: String,
    val playerId: String,
    val questId: String,
    val delta: Int,
    val currentCount: Int,
    val requiredCount: Int,
    val sourceEventId: String,
)

data class QuestCompletedEvent(
    val eventId: String,
    val playerId: String,
    val questId: String,
    val sourceEventId: String,
    val completedAtUnixMs: Long,
)

data class QuestRewardGrantedEvent(
    val eventId: String,
    val playerId: String,
    val questId: String,
    val sourceEventId: String,
    val rewardId: String,
    val grantedAtUnixMs: Long,
)

data class QuestProgressReconciledEvent(
    val eventId: String,
    val playerId: String,
    val questId: String,
    val currentCount: Int,
    val reason: String,
    val reconciledAtUnixMs: Long,
)

data class StoredQuestEvent(
    val eventId: String,
    val sourceEventId: String?,
    val playerId: String,
    val questId: String,
    val eventType: String,
    val payload: ByteArray,
    val version: Long,
    val createdAtUnixMs: Long,
)

@ZLinkPacket("NotifyQuestProgressReq")
data class NotifyQuestProgressReq(
    val playerId: String,
    val projection: List<QuestProgress>,
    val completedQuestId: String?,
)

data class NotifyQuestProgressRes(val delivered: Boolean)

data class GameQuestServerAssertRes(val passed: Boolean, val evidence: List<String>)
