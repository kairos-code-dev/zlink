package systems.zlink.samples.gamequest.client;

import com.fasterxml.jackson.databind.ObjectMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.time.Instant;
import java.util.List;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.gamequest.server.configuration.SampleNames;
import systems.zlink.samples.gamequest.shared.contracts.Messages;
import systems.zlink.stream.connector.ZLinkStreamConnector;
import systems.zlink.stream.connector.ZLinkStreamMessage;

public final class GameQuestClientScenario {
    private final ObjectMapper json = new ObjectMapper();
    private final HttpClient http = HttpClient.newHttpClient();
    private final GameQuestClientOptions options;

    public GameQuestClientScenario(GameQuestClientOptions options) {
        this.options = options;
    }

    public void run(ZLinkStreamConnector apiAStream, ZLinkStreamConnector apiBStream) throws Exception {
        apiAStream.connect().submit().toCompletableFuture().join();
        Messages.JoinSessionRes joined = apiAStream
            .request(new Messages.JoinSessionReq("player-alice"))
            .submit(Messages.JoinSessionRes.class).toCompletableFuture().join();
        ensure(joined.activeQuests().isEmpty());

        CompletionStage<ZLinkStreamMessage<Messages.QuestProgressNotify>> firstProgress =
            apiAStream.waitFor(Messages.QuestProgressNotify.class).submit(Messages.QuestProgressNotify.class);
        Messages.KillMonsterRes firstKill = apiAStream
            .request(new Messages.KillMonsterReq("player-alice", "wolf", "forest", "kill-1"))
            .submit(Messages.KillMonsterRes.class).toCompletableFuture().join();
        ensure(firstKill.eventId().equals("player-alice-kill-1"));
        Messages.QuestProgressNotify firstPush = firstProgress.toCompletableFuture().join().payload();
        ensure(firstPush.playerId().equals("player-alice"));
        ensure(firstPush.progress().questId().equals(Messages.QuestIds.FirstHunt));
        ensure(firstPush.progress().currentCount() == 1);

        Messages.KillMonsterRes firstDuplicate = apiAStream
            .request(new Messages.KillMonsterReq("player-alice", "wolf", "forest", "kill-1"))
            .submit(Messages.KillMonsterRes.class).toCompletableFuture().join();
        ensure(firstDuplicate.eventId().equals(firstKill.eventId()));
        Messages.GetQuestProgressRes progressAfterFirstDuplicate = apiAStream
            .request(new Messages.GetQuestProgressReq("player-alice"))
            .submit(Messages.GetQuestProgressRes.class).toCompletableFuture().join();
        ensure(hasProgress(progressAfterFirstDuplicate.activeQuests(), Messages.QuestIds.FirstHunt, 1));

        CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> firstHuntCompleted =
            apiAStream.waitFor(Messages.QuestCompletedNotify.class)
                .where(Messages.QuestCompletedNotify.class, message ->
                    message.payload().progress().questId().equals(Messages.QuestIds.FirstHunt))
                .submit(Messages.QuestCompletedNotify.class);
        apiAStream.request(new Messages.KillMonsterReq("player-alice", "wolf", "forest", "kill-2"))
            .submit(Messages.KillMonsterRes.class).toCompletableFuture().join();
        Messages.KillMonsterRes thirdKill = apiAStream
            .request(new Messages.KillMonsterReq("player-alice", "wolf", "forest", "kill-3"))
            .submit(Messages.KillMonsterRes.class).toCompletableFuture().join();
        ensure(thirdKill.eventId().equals("player-alice-kill-3"));
        Messages.QuestCompletedNotify firstHuntPush = firstHuntCompleted.toCompletableFuture().join().payload();
        ensure(firstHuntPush.rewardGranted());
        ensure(firstHuntPush.progress().status().equals(Messages.QuestStatuses.RewardGranted));

        Messages.KillMonsterRes duplicate = apiAStream
            .request(new Messages.KillMonsterReq("player-alice", "wolf", "forest", "kill-3"))
            .submit(Messages.KillMonsterRes.class).toCompletableFuture().join();
        ensure(duplicate.eventId().equals(thirdKill.eventId()));

        CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> auctionCompleted =
            apiAStream.waitFor(Messages.QuestCompletedNotify.class)
                .where(Messages.QuestCompletedNotify.class, message ->
                    message.payload().progress().questId().equals(Messages.QuestIds.OpenAuction))
                .submit(Messages.QuestCompletedNotify.class);
        Messages.UnlockFeatureRes auction = apiAStream
            .request(new Messages.UnlockFeatureReq("player-alice", "auction", "unlock-auction"))
            .submit(Messages.UnlockFeatureRes.class).toCompletableFuture().join();
        ensure(auction.eventId().equals("player-alice-unlock-auction"));
        ensure(auctionCompleted.toCompletableFuture().join().payload().rewardGranted());
        Messages.GetGameplaySnapshotRes snapshot = post(
            options.apiAHttpEndpoint(),
            "/internal/snapshot",
            new Messages.GetGameplaySnapshotReq("player-alice"),
            Messages.GetGameplaySnapshotRes.class);
        ensure(snapshot.unlockedFeatureIds().contains("auction"));

        ensure(postRaw(options.missionAHttpEndpoint(), "/self-check/owner/player-alice/close"));
        ensure(postRaw(options.missionBHttpEndpoint(), "/self-check/owner/player-alice/close"));

        Messages.CompleteMissionRes tutorial = apiAStream
            .request(new Messages.CompleteMissionReq("player-alice", "tutorial", "mission-tutorial"))
            .submit(Messages.CompleteMissionRes.class).toCompletableFuture().join();
        ensure(tutorial.eventId().equals("player-alice-mission-tutorial"));
        Messages.EnterAreaRes ruins = apiAStream
            .request(new Messages.EnterAreaReq("player-alice", "ruins", "enter-ruins"))
            .submit(Messages.EnterAreaRes.class).toCompletableFuture().join();
        ensure(ruins.eventId().equals("player-alice-enter-ruins"));

        Messages.CollectItemRes offlineItem = apiAStream
            .request(new Messages.CollectItemReq("player-bob", "healing-herb", 1, "herb-1"))
            .submit(Messages.CollectItemRes.class).toCompletableFuture().join();
        ensure(offlineItem.eventId().equals("player-bob-herb-1"));

        apiBStream.connect().submit().toCompletableFuture().join();
        Messages.JoinSessionRes bobJoined = apiBStream
            .request(new Messages.JoinSessionReq("player-bob"))
            .submit(Messages.JoinSessionRes.class).toCompletableFuture().join();
        ensure(hasProgress(bobJoined.activeQuests(), Messages.QuestIds.HerbGathering, 1));

        CompletionStage<ZLinkStreamMessage<Messages.QuestCompletedNotify>> herbCompleted =
            apiBStream.waitFor(Messages.QuestCompletedNotify.class)
                .where(Messages.QuestCompletedNotify.class, message ->
                    message.payload().progress().questId().equals(Messages.QuestIds.HerbGathering))
                .submit(Messages.QuestCompletedNotify.class);
        Messages.CollectItemRes onlineItem = apiBStream
            .request(new Messages.CollectItemReq("player-bob", "healing-herb", 4, "herb-2"))
            .submit(Messages.CollectItemRes.class).toCompletableFuture().join();
        ensure(onlineItem.eventId().equals("player-bob-herb-2"));
        Messages.QuestCompletedNotify herbPush = herbCompleted.toCompletableFuture().join().payload();
        ensure(herbPush.playerId().equals("player-bob"));
        ensure(herbPush.rewardGranted());
        ensure(herbPush.progress().status().equals(Messages.QuestStatuses.RewardGranted));

        ensure(postRaw(options.apiAHttpEndpoint(),
            "/self-check/projection/player-bob/" + Messages.QuestIds.HerbGathering + "/delete"));
        Messages.GetQuestProgressRes missingProjection = apiBStream
            .request(new Messages.GetQuestProgressReq("player-bob"))
            .submit(Messages.GetQuestProgressRes.class).toCompletableFuture().join();
        ensure(missingProjection.activeQuests().stream()
            .noneMatch(progress -> progress.questId().equals(Messages.QuestIds.HerbGathering)));
        Messages.QuestProgress rebuilt = post(
            options.apiAHttpEndpoint(),
            "/self-check/projection/player-bob/" + Messages.QuestIds.HerbGathering + "/rebuild",
            "",
            Messages.QuestProgress.class);
        ensure(rebuilt.questId().equals(Messages.QuestIds.HerbGathering));
        ensure(rebuilt.status().equals(Messages.QuestStatuses.RewardGranted));
        Messages.GetQuestProgressRes rebuiltProjection = apiBStream
            .request(new Messages.GetQuestProgressReq("player-bob"))
            .submit(Messages.GetQuestProgressRes.class).toCompletableFuture().join();
        ensure(rebuiltProjection.activeQuests().stream().anyMatch(progress ->
            progress.questId().equals(Messages.QuestIds.HerbGathering)
                && progress.status().equals(Messages.QuestStatuses.RewardGranted)));

        ensure(postRaw(options.apiBHttpEndpoint(), "/self-check/gameplay/kill-without-publish/player-alice"));
        ensure(postRaw(options.apiBHttpEndpoint(), "/self-check/gameplay/kill-without-publish/player-alice"));
        Messages.SyncQuestProgressRes sync = apiAStream
            .request(new Messages.SyncQuestProgressReq("player-alice"))
            .submit(Messages.SyncQuestProgressRes.class).toCompletableFuture().join();
        ensure(sync.updatedQuests().stream().anyMatch(progress ->
            progress.questId().equals(Messages.QuestIds.FirstHunt) && progress.currentCount() == 5));
        Messages.GetQuestProgressRes reconciled = apiBStream
            .request(new Messages.GetQuestProgressReq("player-alice"))
            .submit(Messages.GetQuestProgressRes.class).toCompletableFuture().join();
        ensure(reconciled.activeQuests().stream().anyMatch(progress ->
            progress.questId().equals(Messages.QuestIds.FirstHunt) && progress.currentCount() == 5));

        apiAStream.close().submit().toCompletableFuture().join();

        Messages.GameQuestServerAssertRes assertion = waitForServerAssertion();
        ensure(assertion.passed());
        System.out.println(SampleNames.ServerEvidenceMarker);
    }

    private Messages.GameQuestServerAssertRes waitForServerAssertion() throws Exception {
        Instant deadline = Instant.now().plus(Duration.ofSeconds(10));
        Messages.GameQuestServerAssertRes last = null;
        while (Instant.now().isBefore(deadline)) {
            last = post(
                options.apiAHttpEndpoint(),
                "/self-check/assert",
                "",
                Messages.GameQuestServerAssertRes.class);
            if (last.passed()) {
                return last;
            }
        }
        return last;
    }

    private boolean hasProgress(List<Messages.QuestProgress> progress, String questId, int currentCount) {
        return progress.stream()
            .anyMatch(item -> item.questId().equals(questId) && item.currentCount() == currentCount);
    }

    private boolean postRaw(String base, String path) throws Exception {
        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(base + path))
            .POST(HttpRequest.BodyPublishers.ofString(""))
            .build();
        HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
        return response.statusCode() >= 200 && response.statusCode() < 300;
    }

    private <T> T post(String base, String path, Object body, Class<T> type) throws Exception {
        HttpRequest request = HttpRequest.newBuilder()
            .uri(URI.create(base + path))
            .header("content-type", "application/json")
            .POST(HttpRequest.BodyPublishers.ofString(body instanceof String value ? value : json.writeValueAsString(body)))
            .build();
        HttpResponse<String> response = http.send(request, HttpResponse.BodyHandlers.ofString());
        if (response.statusCode() < 200 || response.statusCode() >= 300) {
            throw new IllegalStateException("HTTP " + response.statusCode() + " for " + path + ": "
                + response.body());
        }
        return json.readValue(response.body(), type);
    }

    private static void ensure(boolean condition) {
        if (!condition) {
            throw new IllegalStateException("Ensure failed");
        }
    }
}
