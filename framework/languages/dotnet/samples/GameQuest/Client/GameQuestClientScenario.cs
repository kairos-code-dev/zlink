using System.Runtime.CompilerServices;
using System.Net.Http.Json;
using GameQuest.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Systems.Zlink.Stream.Connector.Json;

namespace GameQuest.Client;

internal sealed class GameQuestClientScenario(GameQuestTopology topology)
{
    private readonly HttpClient _http = new();

    public async ValueTask RunAsync(
        IZlinkStreamConnector apiAStream,
        IZlinkStreamConnector apiBStream,
        CancellationToken cancellationToken = default)
    {
        await apiAStream.ConnectAsync(cancellationToken);
        var subscribed = await apiAStream.Request(new SubscribeQuestReq("player-alice")).SubmitAsync<SubscribeQuestRes>(cancellationToken);
        Ensure(subscribed.ActiveQuests.Length == 0);

        var firstProgress = apiAStream.WaitFor<QuestProgressNotify>().SubmitAsync(cancellationToken);
        var firstKill = await PostAsync<KillMonsterReq, KillMonsterRes>(
            topology.GameApiAHttpBaseUrl,
            "/combat/kill",
            new KillMonsterReq("player-alice", "wolf", "forest", "kill-1"),
            cancellationToken);
        Ensure(firstKill.EventId == "player-alice-kill-1");
        var firstProgressPush = await firstProgress;
        Ensure(firstProgressPush.Payload.PlayerId == "player-alice");
        Ensure(firstProgressPush.Payload.Progress.QuestId == QuestIds.FirstHunt);
        Ensure(firstProgressPush.Payload.Progress.CurrentCount == 1);

        var completeFirstHunt = apiAStream.WaitFor<QuestCompletedNotify>().SubmitAsync(cancellationToken);
        _ = await PostAsync<KillMonsterReq, KillMonsterRes>(
            topology.GameApiAHttpBaseUrl,
            "/combat/kill",
            new KillMonsterReq("player-alice", "wolf", "forest", "kill-2"),
            cancellationToken);
        var thirdKill = await PostAsync<KillMonsterReq, KillMonsterRes>(
            topology.GameApiAHttpBaseUrl,
            "/combat/kill",
            new KillMonsterReq("player-alice", "wolf", "forest", "kill-3"),
            cancellationToken);
        Ensure(thirdKill.EventId == "player-alice-kill-3");
        var completeFirstHuntPush = await completeFirstHunt;
        Ensure(completeFirstHuntPush.Payload.PlayerId == "player-alice");
        Ensure(completeFirstHuntPush.Payload.Progress.QuestId == QuestIds.FirstHunt);
        Ensure(completeFirstHuntPush.Payload.RewardGranted);

        var duplicate = await PostAsync<KillMonsterReq, KillMonsterRes>(
            topology.GameApiAHttpBaseUrl,
            "/combat/kill",
            new KillMonsterReq("player-alice", "wolf", "forest", "kill-3"),
            cancellationToken);
        Ensure(duplicate.EventId == thirdKill.EventId);

        var auctionComplete = apiAStream.WaitFor<QuestCompletedNotify>().SubmitAsync(cancellationToken);
        var auction = await PostAsync<UnlockFeatureReq, UnlockFeatureRes>(
            topology.GameApiAHttpBaseUrl,
            "/feature/unlock",
            new UnlockFeatureReq("player-alice", "auction", "unlock-auction"),
            cancellationToken);
        Ensure(auction.EventId == "player-alice-unlock-auction");
        var auctionCompletePush = await auctionComplete;
        Ensure(auctionCompletePush.Payload.PlayerId == "player-alice");
        Ensure(auctionCompletePush.Payload.Progress.QuestId == QuestIds.OpenAuction);
        Ensure(auctionCompletePush.Payload.RewardGranted);
        var snapshot = await PostAsync<GetGameplaySnapshotReq, GetGameplaySnapshotRes>(
            topology.GameApiAHttpBaseUrl,
            "/internal/snapshot",
            new GetGameplaySnapshotReq("player-alice"),
            cancellationToken);
        Ensure(snapshot.UnlockedFeatureIds.Contains("auction", StringComparer.Ordinal));

        var tutorial = await PostAsync<CompleteMissionReq, CompleteMissionRes>(
            topology.GameApiAHttpBaseUrl,
            "/mission/complete",
            new CompleteMissionReq("player-alice", "tutorial", "mission-tutorial"),
            cancellationToken);
        Ensure(tutorial.EventId == "player-alice-mission-tutorial");

        var ruins = await PostAsync<EnterAreaReq, EnterAreaRes>(
            topology.GameApiAHttpBaseUrl,
            "/world/enter",
            new EnterAreaReq("player-alice", "ruins", "enter-ruins"),
            cancellationToken);
        Ensure(ruins.EventId == "player-alice-enter-ruins");

        var offlineItem = await PostAsync<CollectItemReq, CollectItemRes>(
            topology.GameApiAHttpBaseUrl,
            "/inventory/collect",
            new CollectItemReq("player-bob", "healing-herb", 1, "herb-1"),
            cancellationToken);
        Ensure(offlineItem.EventId == "player-bob-herb-1");

        await apiBStream.ConnectAsync(cancellationToken);
        var bobSubscribed = await apiBStream.Request(new SubscribeQuestReq("player-bob")).SubmitAsync<SubscribeQuestRes>(cancellationToken);
        var bobProgress = bobSubscribed.ActiveQuests.Any(p =>
            p is { QuestId: QuestIds.HerbGathering, CurrentCount: 1 })
            ? bobSubscribed.ActiveQuests
            : await WaitForStreamProjectionAsync(
                apiBStream,
                "player-bob",
                progress => progress is { QuestId: QuestIds.HerbGathering, CurrentCount: 1 },
                cancellationToken);
        Ensure(bobProgress.Any(p => p is { QuestId: QuestIds.HerbGathering, CurrentCount: 1 }));
        var herbCompletedOnReconnectedStream = apiBStream.WaitFor<QuestCompletedNotify>().SubmitAsync(cancellationToken);
        var onlineItem = await PostAsync<CollectItemReq, CollectItemRes>(
            topology.GameApiBHttpBaseUrl,
            "/inventory/collect",
            new CollectItemReq("player-bob", "healing-herb", 4, "herb-2"),
            cancellationToken);
        Ensure(onlineItem.EventId == "player-bob-herb-2");
        var herbCompletedOnReconnectedStreamPush = await herbCompletedOnReconnectedStream;
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.PlayerId == "player-bob");
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.Progress.QuestId == QuestIds.HerbGathering);
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.RewardGranted);
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.Progress.Status == QuestStatuses.RewardGranted);

        await PostNoContentAsync(
            topology.GameApiAHttpBaseUrl,
            $"/self-check/projection/player-bob/{QuestIds.HerbGathering}/delete",
            cancellationToken);
        var missingProjection = await GetStreamProjectionAsync(apiBStream, "player-bob", cancellationToken);
        Ensure(missingProjection.All(progress => progress.QuestId != QuestIds.HerbGathering));
        var rebuilt = await PostEmptyAsync<QuestProgress>(
            topology.GameApiAHttpBaseUrl,
            $"/self-check/projection/player-bob/{QuestIds.HerbGathering}/rebuild",
            cancellationToken);
        Ensure(rebuilt is { QuestId: QuestIds.HerbGathering, Status: QuestStatuses.RewardGranted });
        var rebuiltProjection = await GetStreamProjectionAsync(apiBStream, "player-bob", cancellationToken);
        Ensure(rebuiltProjection.Any(progress =>
                progress is { QuestId: QuestIds.HerbGathering, Status: QuestStatuses.RewardGranted }));

        await PostNoContentAsync(
            topology.GameApiBHttpBaseUrl,
            "/self-check/gameplay/kill-without-publish/player-alice",
            cancellationToken);
        var sync = await apiAStream.Request(new SyncQuestProgressReq("player-alice")).SubmitAsync<SyncQuestProgressRes>(cancellationToken);
        Ensure(sync.UpdatedQuests.Any(progress => progress.QuestId == QuestIds.FirstHunt && progress.CurrentCount >= 4));
        var reconciled = await WaitForProjectionAsync(
            topology.GameApiBHttpBaseUrl,
            "player-alice",
            progress => progress.QuestId == QuestIds.FirstHunt && progress.CurrentCount >= 4,
            cancellationToken);
        Ensure(reconciled.Any(p => p.QuestId == QuestIds.FirstHunt && p.CurrentCount >= 4));

        await apiAStream.DisposeAsync();

        var assertion = await WaitForServerAssertionAsync(cancellationToken);
        Ensure(assertion.Passed);
    }

    private async ValueTask<TResponse> PostAsync<TRequest, TResponse>(
        string baseUrl,
        string path,
        TRequest request,
        CancellationToken cancellationToken)
    {
        var response = await _http.PostAsJsonAsync($"{baseUrl}{path}", request, cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<TResponse>(cancellationToken)
               ?? throw new InvalidOperationException($"Empty response from {path}.");
    }

    private async ValueTask PostNoContentAsync(string baseUrl, string path, CancellationToken cancellationToken)
    {
        var response = await _http.PostAsync($"{baseUrl}{path}", content: null, cancellationToken);
        response.EnsureSuccessStatusCode();
    }

    private async ValueTask<TResponse> PostEmptyAsync<TResponse>(
        string baseUrl,
        string path,
        CancellationToken cancellationToken)
    {
        var response = await _http.PostAsync($"{baseUrl}{path}", content: null, cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<TResponse>(cancellationToken)
               ?? throw new InvalidOperationException($"Empty response from {path}.");
    }

    private async ValueTask<GameQuestServerAssertRes> WaitForServerAssertionAsync(CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + SampleNames.RequestTimeout;
        GameQuestServerAssertRes? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            last = await PostEmptyAsync<GameQuestServerAssertRes>(
                topology.GameApiAHttpBaseUrl,
                "/self-check/assert",
                cancellationToken);
            if (last.Passed)
            {
                return last;
            }

            await Task.Delay(50, cancellationToken);
        }

        return last ?? throw new InvalidOperationException("Server assertion did not return evidence.");
    }

    private async ValueTask<TResponse> GetAsync<TResponse>(
        string baseUrl,
        string path,
        CancellationToken cancellationToken)
    {
        var response = await _http.GetAsync($"{baseUrl}{path}", cancellationToken);
        response.EnsureSuccessStatusCode();
        return await response.Content.ReadFromJsonAsync<TResponse>(cancellationToken)
               ?? throw new InvalidOperationException($"Empty response from {path}.");
    }

    private async ValueTask<QuestProgress[]> WaitForProjectionAsync(
        string baseUrl,
        string playerId,
        Func<QuestProgress, bool> predicate,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + SampleNames.RequestTimeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var response = await GetAsync<GetQuestProgressRes>(
                baseUrl,
                $"/quest/progress/{playerId}",
                cancellationToken);
            if (response.ActiveQuests.Any(predicate))
            {
                return response.ActiveQuests;
            }

            await Task.Delay(50, cancellationToken);
        }

        return (await GetAsync<GetQuestProgressRes>(
            baseUrl,
            $"/quest/progress/{playerId}",
            cancellationToken)).ActiveQuests;
    }

    private static async ValueTask<QuestProgress[]> WaitForStreamProjectionAsync(
        IZlinkStreamConnector connector,
        string playerId,
        Func<QuestProgress, bool> predicate,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + SampleNames.RequestTimeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var response = await connector.Request(new GetQuestProgressReq(playerId)).SubmitAsync<GetQuestProgressRes>(cancellationToken);
            if (response.ActiveQuests.Any(predicate))
            {
                return response.ActiveQuests;
            }

            await Task.Delay(50, cancellationToken);
        }

        return (await connector.Request(new GetQuestProgressReq(playerId)).SubmitAsync<GetQuestProgressRes>(cancellationToken)).ActiveQuests;
    }

    private static async ValueTask<QuestProgress[]> GetStreamProjectionAsync(
        IZlinkStreamConnector connector,
        string playerId,
        CancellationToken cancellationToken) =>
        (await connector.Request(new GetQuestProgressReq(playerId)).SubmitAsync<GetQuestProgressRes>(cancellationToken)).ActiveQuests;

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))] string? expression = null)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"Ensure failed: {expression}");
        }
    }
}
