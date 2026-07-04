package systems.zlink.samples.gamequest.shared.contracts;

import java.util.List;

public final class Messages {
    private Messages() {
    }

    public static final class QuestIds {
        public static final String FirstHunt = "first-hunt";
        public static final String OpenAuction = "open-auction";
        public static final String HerbGathering = "herb-gathering";

        private QuestIds() {
        }
    }

    public static final class QuestStatuses {
        public static final String InProgress = "InProgress";
        public static final String RewardGranted = "RewardGranted";

        private QuestStatuses() {
        }
    }

    public record KillMonsterReq(String playerId, String monsterId, String areaId, String idempotencyKey) {
    }

    public record KillMonsterRes(String eventId) {
    }

    public record CollectItemReq(String playerId, String itemId, int count, String idempotencyKey) {
    }

    public record CollectItemRes(String eventId) {
    }

    public record CompleteMissionReq(String playerId, String missionId, String idempotencyKey) {
    }

    public record CompleteMissionRes(String eventId) {
    }

    public record EnterAreaReq(String playerId, String areaId, String idempotencyKey) {
    }

    public record EnterAreaRes(String eventId) {
    }

    public record UnlockFeatureReq(String playerId, String featureId, String idempotencyKey) {
    }

    public record UnlockFeatureRes(String eventId) {
    }

    public record JoinSessionReq(String playerId) {
    }

    public record JoinSessionRes(List<QuestProgress> activeQuests) {
    }

    public record GetQuestProgressReq(String playerId) {
    }

    public record GetQuestProgressRes(List<QuestProgress> activeQuests) {
    }

    public record DeleteQuestProjectionReq(String playerId, String questId) {
    }

    public record DeleteQuestProjectionRes(boolean deleted) {
    }

    public record RebuildQuestProjectionReq(String playerId, String questId, int count) {
    }

    public record SyncQuestProgressReq(String playerId) {
    }

    public record SyncQuestProgressRes(List<QuestProgress> updatedQuests) {
    }

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

    public record QuestProgressNotify(String playerId, String targetConnectionId, QuestProgress progress) {
    }

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

    public record GameplayEventEnvelope(
        String eventId,
        String playerId,
        String idempotencyKey,
        String eventType,
        String value,
        int count,
        String sourceApi,
        long createdAtUnixMs,
        boolean publish) {
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
        int currentCount,
        int requiredCount,
        String status,
        long version,
        long createdAtUnixMs) {
    }

    public record QuestProcessingRes(
        String eventId,
        List<QuestProgress> projection,
        List<QuestProgressNotify> progressNotifications,
        List<QuestCompletedNotify> completedNotifications,
        boolean duplicate) {
    }

    public record GameQuestServerAssertRes(boolean passed, List<String> evidence) {
    }
}
