package systems.zlink.samples.kotlin.gamequest.server.gameapi.application

import org.springframework.stereotype.Component
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.channels.ZLinkFanoutClient
import systems.zlink.samples.kotlin.gamequest.server.configuration.GameQuestRouting
import systems.zlink.samples.kotlin.gamequest.server.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.server.gameapi.GameApiInstanceOptions
import systems.zlink.samples.kotlin.gamequest.server.gameapi.domain.GameplayDomain
import systems.zlink.samples.kotlin.gamequest.server.gameapi.store.GameQuestStore
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CollectItemReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CollectItemRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CompleteMissionReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CompleteMissionRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.EnterAreaReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.EnterAreaRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameplayEventEnvelope
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillMonsterReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillMonsterRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureRes

/**
 * Stateless gameplay action service. Each action stores an idempotent gameplay
 * event and publishes it over the ZLink fanout channel; the QuestMission server
 * subscribes and advances quest progress. Mirrors the .NET
 * `GameplayActionService`.
 */
@Component
class GameplayActionService(
    private val store: GameQuestStore,
    private val fanout: ZLinkFanoutClient,
    private val channels: ZLinkClient,
    options: GameApiInstanceOptions,
) {
    private val apiName = options.apiName

    fun killMonster(request: KillMonsterReq): KillMonsterRes {
        val published = storeAndPublish(
            GameplayDomain.createMonsterKilled(
                request.playerId, request.monsterId, request.areaId, request.idempotencyKey, apiName,
            ),
        )
        return KillMonsterRes(published.eventId)
    }

    fun collectItem(request: CollectItemReq): CollectItemRes {
        val published = storeAndPublish(
            GameplayDomain.createItemCollected(
                request.playerId, request.itemId, request.count, request.idempotencyKey, apiName,
            ),
        )
        return CollectItemRes(published.eventId)
    }

    fun completeMission(request: CompleteMissionReq): CompleteMissionRes {
        val published = storeAndPublish(
            GameplayDomain.createMissionCompleted(
                request.playerId, request.missionId, request.idempotencyKey, apiName,
            ),
        )
        return CompleteMissionRes(published.eventId)
    }

    fun enterArea(request: EnterAreaReq): EnterAreaRes {
        val published = storeAndPublish(
            GameplayDomain.createAreaEntered(
                request.playerId, request.areaId, request.idempotencyKey, apiName,
            ),
        )
        return EnterAreaRes(published.eventId)
    }

    fun unlockFeature(request: UnlockFeatureReq): UnlockFeatureRes {
        val published = storeAndPublish(
            GameplayDomain.createFeatureUnlocked(
                request.playerId, request.featureId, request.idempotencyKey, apiName,
            ),
        )
        return UnlockFeatureRes(published.eventId)
    }

    fun sync(playerId: String): SyncQuestProgressRes {
        val missionName = if (GameQuestRouting.ownerIndex(playerId) == 1) "mission-b" else "mission-a"
        return channels
            .requestToChannel(
                SampleNames.questMissionChannel(missionName),
                SyncQuestProgressReq(playerId),
            )
            .timeout(SampleNames.RequestTimeout)
            .await(SyncQuestProgressRes::class.java)
    }

    private fun storeAndPublish(candidate: GameplayEventEnvelope): GameplayEventEnvelope {
        val stored = store.getOrAddGameplayEvent(candidate)
        fanout.publish(SampleNames.FanoutChannel, SampleNames.GameplayTopic, stored).await()
        System.err.printf(
            "gamequest api event published api=%s player=%s event=%s type=%s%n",
            apiName, stored.playerId, stored.eventId, stored.eventType,
        )
        return stored
    }
}
