package systems.zlink.samples.gamequest.server.gameapi.domain;

import systems.zlink.samples.gamequest.shared.contracts.Messages;

/** Factory for gameplay event envelopes. Mirrors the .NET {@code GameplayDomain}. */
public final class GameplayDomain {
    private GameplayDomain() {
    }

    public static Messages.GameplayEventEnvelope createMonsterKilled(
        String playerId, String monsterId, String areaId, String idempotencyKey, String sourceApi) {
        return create(playerId, idempotencyKey, "MonsterKilled", monsterId, 1, sourceApi);
    }

    public static Messages.GameplayEventEnvelope createItemCollected(
        String playerId, String itemId, int count, String idempotencyKey, String sourceApi) {
        return create(playerId, idempotencyKey, "ItemCollected", itemId, count, sourceApi);
    }

    public static Messages.GameplayEventEnvelope createMissionCompleted(
        String playerId, String missionId, String idempotencyKey, String sourceApi) {
        return create(playerId, idempotencyKey, "MissionCompleted", missionId, 1, sourceApi);
    }

    public static Messages.GameplayEventEnvelope createAreaEntered(
        String playerId, String areaId, String idempotencyKey, String sourceApi) {
        return create(playerId, idempotencyKey, "AreaEntered", areaId, 1, sourceApi);
    }

    public static Messages.GameplayEventEnvelope createFeatureUnlocked(
        String playerId, String featureId, String idempotencyKey, String sourceApi) {
        return create(playerId, idempotencyKey, "FeatureUnlocked", featureId, 1, sourceApi);
    }

    private static Messages.GameplayEventEnvelope create(
        String playerId, String idempotencyKey, String eventType, String value, int count, String sourceApi) {
        if (playerId == null || playerId.isBlank()) {
            throw new IllegalStateException("Player id is required.");
        }
        if (idempotencyKey == null || idempotencyKey.isBlank()) {
            throw new IllegalStateException("Idempotency key is required.");
        }
        if (count <= 0) {
            throw new IllegalStateException("Count must be positive.");
        }
        return new Messages.GameplayEventEnvelope(
            playerId + "-" + idempotencyKey,
            playerId,
            idempotencyKey,
            eventType,
            value,
            count,
            sourceApi,
            System.currentTimeMillis());
    }
}
