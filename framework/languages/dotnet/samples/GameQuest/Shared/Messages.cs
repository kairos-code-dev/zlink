namespace GameQuest.Shared;

public sealed record KillMonsterReq(string PlayerId, string MonsterId, string AreaId, string IdempotencyKey);

public sealed record KillMonsterRes(string EventId);

public sealed record CollectItemReq(string PlayerId, string ItemId, int Count, string IdempotencyKey);

public sealed record CollectItemRes(string EventId);

public sealed record CompleteMissionReq(string PlayerId, string MissionId, string IdempotencyKey);

public sealed record CompleteMissionRes(string EventId);

public sealed record EnterAreaReq(string PlayerId, string AreaId, string IdempotencyKey);

public sealed record EnterAreaRes(string EventId);

public sealed record UnlockFeatureReq(string PlayerId, string FeatureId, string IdempotencyKey);

public sealed record UnlockFeatureRes(string EventId);

public sealed record JoinSessionReq(string PlayerId);

public sealed record JoinSessionRes(QuestProgress[] ActiveQuests);

public sealed record GetQuestProgressReq(string PlayerId);

public sealed record GetQuestProgressRes(QuestProgress[] ActiveQuests);

public sealed record SyncQuestProgressReq(string PlayerId);

public sealed record SyncQuestProgressRes(QuestProgress[] UpdatedQuests);

public sealed record ClosePlayerQuestOwnerReq(string PlayerId);

public sealed record ClosePlayerQuestOwnerRes(bool Closed);

public sealed record GetGameplaySnapshotReq(string PlayerId);

public sealed record GetGameplaySnapshotRes(
    string PlayerId,
    KillCountSnapshot[] KillCounts,
    ItemCountSnapshot[] ItemCounts,
    string[] CompletedMissionIds,
    string[] UnlockedFeatureIds,
    string[] EnteredAreaIds,
    long SnapshotVersion);

public sealed record KillCountSnapshot(string MonsterId, string? AreaId, int Count);

public sealed record ItemCountSnapshot(string ItemId, int Count);

public sealed record QuestProgressNotify(string PlayerId, QuestProgress Progress);

public sealed record QuestCompletedNotify(
    string PlayerId,
    QuestProgress Progress,
    bool RewardGranted);

public sealed record QuestProgress(
    string PlayerId,
    string QuestId,
    string Status,
    int CurrentCount,
    int RequiredCount,
    string? LastEventId,
    long UpdatedAtUnixMs);

public sealed record GameplayEventEnvelope(
    string EventId,
    string PlayerId,
    string IdempotencyKey,
    string EventType,
    string Value,
    int Count,
    string SourceApi,
    long CreatedAtUnixMs);

public sealed record QuestProgressedEvent(
    string EventId,
    string PlayerId,
    string QuestId,
    int Delta,
    int CurrentCount,
    int RequiredCount,
    string SourceEventId);

public sealed record QuestCompletedEvent(
    string EventId,
    string PlayerId,
    string QuestId,
    string SourceEventId,
    long CompletedAtUnixMs);

public sealed record QuestRewardGrantedEvent(
    string EventId,
    string PlayerId,
    string QuestId,
    string SourceEventId,
    string RewardId,
    long GrantedAtUnixMs);

public sealed record QuestProgressReconciledEvent(
    string EventId,
    string PlayerId,
    string QuestId,
    int CurrentCount,
    string Reason,
    long ReconciledAtUnixMs);

public sealed record StoredQuestEvent(
    string EventId,
    string? SourceEventId,
    string PlayerId,
    string QuestId,
    string EventType,
    byte[] Payload,
    long Version,
    long CreatedAtUnixMs);

public sealed record NotifyQuestProgressReq(string PlayerId, QuestProgress[] Projection, string? CompletedQuestId);

public sealed record NotifyQuestProgressRes(bool Delivered);

public sealed record GameQuestServerAssertRes(
    bool Passed,
    string[] Evidence);
