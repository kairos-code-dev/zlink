package systems.zlink.samples.kotlin.gamequest.client

import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.future.await as futureAwait
import systems.zlink.framework.channels.ZLinkClient
import systems.zlink.framework.kotlin.ZLinkKotlinStreamConnector
import systems.zlink.framework.kotlin.await
import systems.zlink.samples.kotlin.gamequest.client.configuration.QuestIds
import systems.zlink.samples.kotlin.gamequest.client.configuration.QuestStatuses
import systems.zlink.samples.kotlin.gamequest.client.configuration.SampleNames
import systems.zlink.samples.kotlin.gamequest.client.configuration.SampleTimings
import systems.zlink.samples.kotlin.gamequest.client.contracts.DeleteProjectionReq
import systems.zlink.samples.kotlin.gamequest.client.contracts.DeleteProjectionRes
import systems.zlink.samples.kotlin.gamequest.client.contracts.GameQuestServerAssertReq
import systems.zlink.samples.kotlin.gamequest.client.contracts.KillWithoutPublishReq
import systems.zlink.samples.kotlin.gamequest.client.contracts.KillWithoutPublishRes
import systems.zlink.samples.kotlin.gamequest.client.contracts.RebuildProjectionReq
import systems.zlink.samples.kotlin.gamequest.client.contracts.RebuildProjectionRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CollectItemReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.CollectItemRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GameQuestServerAssertRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.GetGameplaySnapshotRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillMonsterReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.KillMonsterRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestCompletedNotify
import systems.zlink.samples.kotlin.gamequest.shared.contracts.QuestProgress
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SubscribeQuestReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SubscribeQuestRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.SyncQuestProgressRes
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureReq
import systems.zlink.samples.kotlin.gamequest.shared.contracts.UnlockFeatureRes
import systems.zlink.stream.connector.ZLinkStreamMessage

class GameQuestClientScenario(
    private val channels: ZLinkClient,
) {
    suspend fun run(alice: ZLinkKotlinStreamConnector, bob: ZLinkKotlinStreamConnector) {
        alice.connect().await()
        bob.connect().await()

        subscribe(alice, Alice)
        subscribe(bob, Bob)

        runFirstHunt(alice)
        runOpenAuction(alice)
        runDuplicateEvent()
        runProjectionRebuild()
        runHerbGathering(bob)
        runSnapshotReconciliation(alice)

        alice.close().await()
        assertServerEvidence()
    }

    private suspend fun runFirstHunt(alice: ZLinkKotlinStreamConnector) = coroutineScope {
        val completed = awaitCompleted(alice, Alice, QuestIds.FirstHunt)

        val first = kill(SampleNames.ApiA, "wolf-1")
        ensure(first.eventId.isNotBlank())
        val second = kill(SampleNames.ApiA, "wolf-2")
        ensure(second.eventId.isNotBlank())
        val third = kill(SampleNames.ApiA, "wolf-3")
        ensure(third.eventId.isNotBlank())

        val notify = completed.await().payload()
        ensure(notify.progress.questId == QuestIds.FirstHunt)
        ensure(notify.progress.status == QuestStatuses.RewardGranted)
        ensure(notify.progress.currentCount == 3)
        println("gamequest-first-hunt=completed")
    }

    private suspend fun runOpenAuction(alice: ZLinkKotlinStreamConnector) = coroutineScope {
        val completed = awaitCompleted(alice, Alice, QuestIds.OpenAuction)
        val unlocked = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                UnlockFeatureReq(Alice, "auction", "alice-auction-001"),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(UnlockFeatureRes::class.java)
            .futureAwait()
        ensure(unlocked.eventId.isNotBlank())
        ensure(completed.await().payload().progress.questId == QuestIds.OpenAuction)

        val snapshot = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiB),
                GetGameplaySnapshotReq(Alice),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(GetGameplaySnapshotRes::class.java)
            .futureAwait()
        ensure(snapshot.unlockedFeatureIds.contains("auction"))
        println("gamequest-open-auction=completed")
    }

    private suspend fun runDuplicateEvent() {
        val duplicate = kill(SampleNames.ApiB, "wolf-duplicate")
        val duplicateAgain = kill(SampleNames.ApiB, "wolf-duplicate")
        ensure(duplicate.eventId == duplicateAgain.eventId)
        println("gamequest-idempotency=completed")
    }

    private suspend fun runProjectionRebuild() {
        val deleted = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                DeleteProjectionReq(Alice, QuestIds.FirstHunt),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(DeleteProjectionRes::class.java)
            .futureAwait()
        ensure(deleted.deleted)

        val rebuilt = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                RebuildProjectionReq(Alice, QuestIds.FirstHunt),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(RebuildProjectionRes::class.java)
            .futureAwait()
        ensure(rebuilt.state.status == QuestStatuses.RewardGranted)
        println("gamequest-rebuild=completed")
    }

    private suspend fun runHerbGathering(bob: ZLinkKotlinStreamConnector) = coroutineScope {
        val completed = awaitCompleted(bob, Bob, QuestIds.HerbGathering)
        val collected = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiB),
                CollectItemReq(Bob, "healing-herb", 5, "bob-herbs-001"),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(CollectItemRes::class.java)
            .futureAwait()
        ensure(collected.eventId.isNotBlank())
        val notify = completed.await().payload()
        ensure(notify.progress.questId == QuestIds.HerbGathering)
        ensure(notify.progress.status == QuestStatuses.RewardGranted)
        println("gamequest-herb-gathering=completed")
    }

    private suspend fun runSnapshotReconciliation(alice: ZLinkKotlinStreamConnector) {
        val accepted = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                KillWithoutPublishReq(Alice),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(KillWithoutPublishRes::class.java)
            .futureAwait()
        ensure(accepted.accepted)

        val synced = alice.request(SyncQuestProgressReq(Alice)).await<SyncQuestProgressRes>()
        ensure(hasQuest(synced.updatedQuests, QuestIds.FirstHunt, QuestStatuses.RewardGranted))
        println("gamequest-sync=completed")
    }

    private suspend fun assertServerEvidence() {
        var assertion: GameQuestServerAssertRes? = null
        val apis = listOf(SampleNames.ApiA, SampleNames.ApiB)
        val deadline = System.nanoTime() + SampleTimings.RequestTimeout.toNanos()
        var attempt = 0
        while (System.nanoTime() < deadline) {
            val apiName = apis[attempt % apis.size]
            assertion = try {
                channels
                    .requestToChannel(
                        SampleNames.gameApiActionChannel(apiName),
                        GameQuestServerAssertReq(),
                    )
                    .timeout(SampleTimings.AssertAttemptTimeout)
                    .submit(GameQuestServerAssertRes::class.java)
                    .futureAwait()
            } catch (_: Exception) {
                null
            }
            if (assertion?.passed == true) {
                break
            }
            attempt += 1
            delay(SampleTimings.PollDelay.toMillis())
        }

        val passedAssertion = assertion ?: throw IllegalStateException("Ensure failed")
        ensure(passedAssertion.passed)
        ensure(passedAssertion.evidence.any { it.contains("player-alice") })
        ensure(passedAssertion.evidence.any { it.contains("player-bob") })
        println("gamequest-server-evidence=completed")
    }

    private suspend fun subscribe(connector: ZLinkKotlinStreamConnector, playerId: String) {
        connector.request(SubscribeQuestReq(playerId)).await<SubscribeQuestRes>()
    }

    private suspend fun kill(apiName: String, idempotencyKey: String): KillMonsterRes =
        channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(apiName),
                KillMonsterReq(Alice, "wolf", "forest", idempotencyKey),
            )
            .timeout(SampleTimings.RequestTimeout)
            .submit(KillMonsterRes::class.java)
            .futureAwait()

    private fun kotlinx.coroutines.CoroutineScope.awaitCompleted(
        connector: ZLinkKotlinStreamConnector,
        playerId: String,
        questId: String,
    ): Deferred<ZLinkStreamMessage<QuestCompletedNotify>> {
        val wait = connector.waitFor<QuestCompletedNotify>()
            .where { it.payload().playerId == playerId && it.payload().progress.questId == questId }
        return async { wait.await() }
    }

    private fun hasQuest(quests: List<QuestProgress>, questId: String, status: String): Boolean =
        quests.any { it.questId == questId && it.status == status }

    private fun ensure(condition: Boolean) {
        if (!condition) {
            throw IllegalStateException("Ensure failed")
        }
    }

    companion object {
        private const val Alice = "player-alice"
        private const val Bob = "player-bob"
    }
}
