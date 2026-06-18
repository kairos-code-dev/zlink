package systems.zlink.samples.gamequest.shared.contracts;

import java.util.List;
import systems.zlink.framework.handlers.ZLinkPacket;

/**
 * Wire contracts for the GameQuest sample.
 *
 * <p>The sample mirrors the .NET GameQuest scenario over ZLink. Stateless
 * {@code GameApi} servers own the action API and WebSocket session surface and
 * publish gameplay events over ZLink fanout. The stateful {@code QuestMission}
 * server owns {@code PlayerQuestSpot} aggregates, event-sourced quest state, and
 * the read-model projection used for client notifications.
 */
public final class Messages {
    private Messages() {
    }

    @ZLinkPacket("KillMonsterReq")
    public record KillMonsterReq(String playerId, String monsterId, String areaId, String idempotencyKey) {
    }

    public record KillMonsterRes(String eventId) {
    }

    @ZLinkPacket("CollectItemReq")
    public record CollectItemReq(String playerId, String itemId, int count, String idempotencyKey) {
    }

    public record CollectItemRes(String eventId) {
    }

    @ZLinkPacket("CompleteMissionReq")
    public record CompleteMissionReq(String playerId, String missionId, String idempotencyKey) {
    }

    public record CompleteMissionRes(String eventId) {
    }

    @ZLinkPacket("EnterAreaReq")
    public record EnterAreaReq(String playerId, String areaId, String idempotencyKey) {
    }

    public record EnterAreaRes(String eventId) {
    }

    @ZLinkPacket("UnlockFeatureReq")
    public record UnlockFeatureReq(String playerId, String featureId, String idempotencyKey) {
    }

    public record UnlockFeatureRes(String eventId) {
    }

    @ZLinkPacket("SubscribeQuestReq")
    public record SubscribeQuestReq(String playerId) {
    }

    public record SubscribeQuestRes(List<QuestProgress> activeQuests) {
    }

    @ZLinkPacket("GetQuestProgressReq")
    public record GetQuestProgressReq(String playerId) {
    }

    public record GetQuestProgressRes(List<QuestProgress> activeQuests) {
    }

    @ZLinkPacket("SyncQuestProgressReq")
    public record SyncQuestProgressReq(String playerId) {
    }

    public record SyncQuestProgressRes(List<QuestProgress> updatedQuests) {
    }

    @ZLinkPacket("GetGameplaySnapshotReq")
    public record GetGameplaySnapshotReq(String playerId) {
    }

    public record GetGameplaySnapshotRes(
        String playerId,
        List<KillCountSnapshot> killCounts,
        List<ItemCountSnapshot> itemCounts,
        List<String> completedMissionIds,
        List<String> unlockedFeatureIds,
        List<String> enteredAreaIds,
        long snapshotVersion) {
    }

    public record KillCountSnapshot(String monsterId, String areaId, int count) {
    }

    public record ItemCountSnapshot(String itemId, int count) {
    }

    @ZLinkPacket("BindQuestSessionReq")
    public record BindQuestSessionReq(String playerId, String connectionId, String gameApiInstanceId) {
    }

    public record BindQuestSessionRes(boolean bound) {
    }

    @ZLinkPacket("UnbindQuestSessionReq")
    public record UnbindQuestSessionReq(String playerId, String connectionId) {
    }

    public record UnbindQuestSessionRes(boolean unbound) {
    }

    @ZLinkPacket("QuestProgressNotify")
    public record QuestProgressNotify(String playerId, String targetConnectionId, QuestProgress progress) {
    }

    @ZLinkPacket("QuestCompletedNotify")
    public record QuestCompletedNotify(
        String playerId,
        String targetConnectionId,
        QuestProgress progress,
        boolean rewardGranted) {
    }

    public record QuestProgress(
        String playerId,
        String questId,
        String status,
        int currentCount,
        int requiredCount,
        String lastEventId,
        long updatedAtUnixMs) {
    }

    @ZLinkPacket("GameplayEventEnvelope")
    public record GameplayEventEnvelope(
        String eventId,
        String playerId,
        String idempotencyKey,
        String eventType,
        String value,
        int count,
        String sourceApi,
        long createdAtUnixMs) {
    }

    public record QuestProgressedEvent(
        String eventId,
        String playerId,
        String questId,
        int delta,
        int currentCount,
        int requiredCount,
        String sourceEventId) {
    }

    public record QuestCompletedEvent(
        String eventId,
        String playerId,
        String questId,
        String sourceEventId,
        long completedAtUnixMs) {
    }

    public record QuestRewardGrantedEvent(
        String eventId,
        String playerId,
        String questId,
        String sourceEventId,
        String rewardId,
        long grantedAtUnixMs) {
    }

    public record QuestProgressReconciledEvent(
        String eventId,
        String playerId,
        String questId,
        int currentCount,
        String reason,
        long reconciledAtUnixMs) {
    }

    public record StoredQuestEvent(
        String eventId,
        String sourceEventId,
        String playerId,
        String questId,
        String eventType,
        byte[] payload,
        long version,
        long createdAtUnixMs) {
    }

    @ZLinkPacket("NotifyQuestProgressReq")
    public record NotifyQuestProgressReq(String playerId, List<QuestProgress> projection, String completedQuestId) {
    }

    public record NotifyQuestProgressRes(boolean delivered) {
    }

    public record GameQuestServerAssertRes(boolean passed, List<String> evidence) {
    }
}
