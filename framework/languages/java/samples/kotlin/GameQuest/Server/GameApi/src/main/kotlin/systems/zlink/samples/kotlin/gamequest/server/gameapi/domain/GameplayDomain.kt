package systems.zlink.samples.kotlin.gamequest.server.gameapi.domain

import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventEnvelope

/** Factory for gameplay event envelopes. Mirrors the .NET `GameplayDomain`. */
object GameplayDomain {
    fun createMonsterKilled(
        playerId: String,
        monsterId: String,
        areaId: String,
        idempotencyKey: String,
        sourceApi: String,
    ): GameplayEventEnvelope =
        create(playerId, idempotencyKey, "MonsterKilled", monsterId, 1, sourceApi)

    fun createItemCollected(
        playerId: String,
        itemId: String,
        count: Int,
        idempotencyKey: String,
        sourceApi: String,
    ): GameplayEventEnvelope =
        create(playerId, idempotencyKey, "ItemCollected", itemId, count, sourceApi)

    fun createMissionCompleted(
        playerId: String,
        missionId: String,
        idempotencyKey: String,
        sourceApi: String,
    ): GameplayEventEnvelope =
        create(playerId, idempotencyKey, "MissionCompleted", missionId, 1, sourceApi)

    fun createAreaEntered(
        playerId: String,
        areaId: String,
        idempotencyKey: String,
        sourceApi: String,
    ): GameplayEventEnvelope =
        create(playerId, idempotencyKey, "AreaEntered", areaId, 1, sourceApi)

    fun createFeatureUnlocked(
        playerId: String,
        featureId: String,
        idempotencyKey: String,
        sourceApi: String,
    ): GameplayEventEnvelope =
        create(playerId, idempotencyKey, "FeatureUnlocked", featureId, 1, sourceApi)

    private fun create(
        playerId: String,
        idempotencyKey: String,
        eventType: String,
        value: String,
        count: Int,
        sourceApi: String,
    ): GameplayEventEnvelope {
        check(playerId.isNotBlank()) { "Player id is required." }
        check(idempotencyKey.isNotBlank()) { "Idempotency key is required." }
        check(count > 0) { "Count must be positive." }
        return GameplayEventEnvelope(
            "$playerId-$idempotencyKey",
            playerId,
            idempotencyKey,
            eventType,
            value,
            count,
            sourceApi,
            System.currentTimeMillis(),
        )
    }
}
