package systems.zlink.samples.gamequest.client;

import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.locks.LockSupport;
import systems.zlink.framework.channels.ZLinkClient;
import systems.zlink.samples.gamequest.client.configuration.QuestIds;
import systems.zlink.samples.gamequest.client.configuration.QuestStatuses;
import systems.zlink.samples.gamequest.client.configuration.SampleNames;
import systems.zlink.samples.gamequest.client.configuration.SampleTimings;
import systems.zlink.samples.gamequest.client.contracts.SelfCheckMessages;
import systems.zlink.samples.gamequest.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class GameQuestClientScenario {
    private static final String Alice = "player-alice";
    private static final String Bob = "player-bob";

    private final ZLinkClient channels;

    public GameQuestClientScenario(ZLinkClient channels) {
        this.channels = channels;
    }

    public void run(ZLinkStreamConnector alice, ZLinkStreamConnector bob) throws Exception {
        alice.connect().await();
        bob.connect().await();

        subscribe(alice, Alice);
        subscribe(bob, Bob);

        runFirstHunt(alice);
        runOpenAuction(alice);
        runDuplicateEvent();
        runProjectionRebuild();
        runHerbGathering(bob);
        runSnapshotReconciliation(alice);

        alice.close().await();
        assertServerEvidence();
    }

    private void runFirstHunt(ZLinkStreamConnector alice) throws Exception {
        CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> completed =
            awaitCompleted(alice, Alice, QuestIds.FirstHunt);

        Messages.KillMonsterRes first = kill(SampleNames.ApiA, "wolf-1");
        ensure(!first.eventId().isBlank());
        Messages.KillMonsterRes second = kill(SampleNames.ApiA, "wolf-2");
        ensure(!second.eventId().isBlank());
        Messages.KillMonsterRes third = kill(SampleNames.ApiA, "wolf-3");
        ensure(!third.eventId().isBlank());

        Messages.QuestCompletedNotify notify = alice.await(completed).payload();
        ensure(QuestIds.FirstHunt.equals(notify.progress().questId()));
        ensure(QuestStatuses.RewardGranted.equals(notify.progress().status()));
        ensure(notify.progress().currentCount() == 3);
        System.out.println("gamequest-first-hunt=completed");
    }

    private void runOpenAuction(ZLinkStreamConnector alice) throws Exception {
        CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> completed =
            awaitCompleted(alice, Alice, QuestIds.OpenAuction);
        Messages.UnlockFeatureRes unlocked = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                new Messages.UnlockFeatureReq(Alice, "auction", "alice-auction-001"))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.UnlockFeatureRes.class);
        ensure(!unlocked.eventId().isBlank());
        ensure(QuestIds.OpenAuction.equals(alice.await(completed).payload().progress().questId()));

        Messages.GetGameplaySnapshotRes snapshot = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiB),
                new Messages.GetGameplaySnapshotReq(Alice))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.GetGameplaySnapshotRes.class);
        ensure(snapshot.unlockedFeatureIds().contains("auction"));
        System.out.println("gamequest-open-auction=completed");
    }

    private void runDuplicateEvent() {
        Messages.KillMonsterRes duplicate = kill(SampleNames.ApiB, "wolf-duplicate");
        Messages.KillMonsterRes duplicateAgain = kill(SampleNames.ApiB, "wolf-duplicate");
        ensure(duplicate.eventId().equals(duplicateAgain.eventId()));
        System.out.println("gamequest-idempotency=completed");
    }

    private void runProjectionRebuild() {
        SelfCheckMessages.DeleteProjectionRes deleted = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                new SelfCheckMessages.DeleteProjectionReq(Alice, QuestIds.FirstHunt))
            .timeout(SampleTimings.RequestTimeout)
            .await(SelfCheckMessages.DeleteProjectionRes.class);
        ensure(deleted.deleted());

        SelfCheckMessages.RebuildProjectionRes rebuilt = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                new SelfCheckMessages.RebuildProjectionReq(Alice, QuestIds.FirstHunt))
            .timeout(SampleTimings.RequestTimeout)
            .await(SelfCheckMessages.RebuildProjectionRes.class);
        ensure(QuestStatuses.RewardGranted.equals(rebuilt.state().status()));
        System.out.println("gamequest-rebuild=completed");
    }

    private void runHerbGathering(ZLinkStreamConnector bob) throws Exception {
        CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> completed =
            awaitCompleted(bob, Bob, QuestIds.HerbGathering);
        Messages.CollectItemRes collected = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiB),
                new Messages.CollectItemReq(Bob, "healing-herb", 5, "bob-herbs-001"))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.CollectItemRes.class);
        ensure(!collected.eventId().isBlank());
        Messages.QuestCompletedNotify notify = bob.await(completed).payload();
        ensure(QuestIds.HerbGathering.equals(notify.progress().questId()));
        ensure(QuestStatuses.RewardGranted.equals(notify.progress().status()));
        System.out.println("gamequest-herb-gathering=completed");
    }

    private void runSnapshotReconciliation(ZLinkStreamConnector alice) throws Exception {
        SelfCheckMessages.KillWithoutPublishRes accepted = channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(SampleNames.ApiA),
                new SelfCheckMessages.KillWithoutPublishReq(Alice))
            .timeout(SampleTimings.RequestTimeout)
            .await(SelfCheckMessages.KillWithoutPublishRes.class);
        ensure(accepted.accepted());

        Messages.SyncQuestProgressRes synced = alice
            .request(new Messages.SyncQuestProgressReq(Alice))
            .await(Messages.SyncQuestProgressRes.class);
        ensure(hasQuest(synced.updatedQuests(), QuestIds.FirstHunt, QuestStatuses.RewardGranted));
        System.out.println("gamequest-sync=completed");
    }

    private void assertServerEvidence() {
        Messages.GameQuestServerAssertRes assertion = null;
        long deadline = System.nanoTime() + SampleTimings.RequestTimeout.toNanos();
        while (System.nanoTime() < deadline) {
            assertion = channels
                .requestToChannel(
                    SampleNames.gameApiActionChannel(SampleNames.ApiA),
                    new SelfCheckMessages.GameQuestServerAssertReq())
                .timeout(SampleTimings.RequestTimeout)
                .await(Messages.GameQuestServerAssertRes.class);
            if (assertion.passed()) {
                break;
            }
            LockSupport.parkNanos(SampleTimings.PollDelay.toNanos());
        }

        ensure(assertion != null && assertion.passed(), assertion);
        ensure(assertion.evidence().stream().anyMatch(line -> line.contains("player-alice")));
        ensure(assertion.evidence().stream().anyMatch(line -> line.contains("player-bob")));
        System.out.println("gamequest-server-evidence=completed");
    }

    private void subscribe(ZLinkStreamConnector connector, String playerId) throws Exception {
        Messages.SubscribeQuestRes subscribed = connector
            .request(new Messages.SubscribeQuestReq(playerId))
            .await(Messages.SubscribeQuestRes.class);
        ensure(subscribed.activeQuests() != null);
    }

    private Messages.KillMonsterRes kill(String apiName, String idempotencyKey) {
        return channels
            .requestToChannel(
                SampleNames.gameApiActionChannel(apiName),
                new Messages.KillMonsterReq(Alice, "wolf", "forest", idempotencyKey))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.KillMonsterRes.class);
    }

    private CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> awaitCompleted(
        ZLinkStreamConnector connector,
        String playerId,
        String questId) {
        return connector.waitFor(Messages.QuestCompletedNotify.class)
            .where(
                Messages.QuestCompletedNotify.class,
                message -> message.payload().playerId().equals(playerId)
                    && message.payload().progress().questId().equals(questId))
            .submit(Messages.QuestCompletedNotify.class);
    }

    private static boolean hasQuest(List<Messages.QuestProgress> quests, String questId, String status) {
        return quests.stream().anyMatch(quest ->
            quest.questId().equals(questId) && quest.status().equals(status));
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }

    private static void ensure(boolean condition, Messages.GameQuestServerAssertRes assertion) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed: "
                + (assertion == null ? "no assertion response" : assertion.evidence()));
        }
    }
}
