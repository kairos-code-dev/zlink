using GameQuest.Shared;

namespace GameQuest.GameApi.Domain;

internal static class GameplayDomain
{
    public static GameplayEventEnvelope CreateMonsterKilled(
        string playerId,
        string monsterId,
        string areaId,
        string idempotencyKey,
        string sourceApi) =>
        Create(playerId, idempotencyKey, "MonsterKilled", monsterId, 1, sourceApi);

    public static GameplayEventEnvelope CreateItemCollected(
        string playerId,
        string itemId,
        int count,
        string idempotencyKey,
        string sourceApi) =>
        Create(playerId, idempotencyKey, "ItemCollected", itemId, count, sourceApi);

    public static GameplayEventEnvelope CreateMissionCompleted(
        string playerId,
        string missionId,
        string idempotencyKey,
        string sourceApi) =>
        Create(playerId, idempotencyKey, "MissionCompleted", missionId, 1, sourceApi);

    public static GameplayEventEnvelope CreateAreaEntered(
        string playerId,
        string areaId,
        string idempotencyKey,
        string sourceApi) =>
        Create(playerId, idempotencyKey, "AreaEntered", areaId, 1, sourceApi);

    public static GameplayEventEnvelope CreateFeatureUnlocked(
        string playerId,
        string featureId,
        string idempotencyKey,
        string sourceApi) =>
        Create(playerId, idempotencyKey, "FeatureUnlocked", featureId, 1, sourceApi);

    private static GameplayEventEnvelope Create(
        string playerId,
        string idempotencyKey,
        string eventType,
        string value,
        int count,
        string sourceApi)
    {
        if (string.IsNullOrWhiteSpace(playerId))
        {
            throw new InvalidOperationException("Player id is required.");
        }

        if (string.IsNullOrWhiteSpace(idempotencyKey))
        {
            throw new InvalidOperationException("Idempotency key is required.");
        }

        if (count <= 0)
        {
            throw new InvalidOperationException("Count must be positive.");
        }

        return new GameplayEventEnvelope(
            $"{playerId}-{idempotencyKey}",
            playerId,
            idempotencyKey,
            eventType,
            value,
            count,
            sourceApi,
            DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
    }
}
