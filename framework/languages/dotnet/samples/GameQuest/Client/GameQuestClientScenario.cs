using System.Runtime.CompilerServices;
using GameQuest.Client.Configuration;
using GameQuest.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace GameQuest.Client;

internal sealed class GameQuestClientScenario(GameQuestTopology topology)
{
    // End-to-end client story:
    // 1. Subscribe player-alice on API A and verify quest progress/completion from combat events.
    // 2. Replay a duplicate event and confirm the same event id is treated idempotently.
    // 3. Complete feature, mission, and area events that unlock later quest progress.
    // 4. Create offline progress for player-bob, reconnect on API B, and finish the herb quest.
    // 5. Delete and rebuild a projection to prove stream queries see recovered quest state.
    // 6. Reconcile a missed publish through sync, then ask the server for final evidence.
    public async ValueTask RunAsync(
        IZlinkStreamConnector apiAStream,
        IZlinkStreamConnector apiBStream,
        CancellationToken cancellationToken = default)
    {
        using var apiA = ZLinkHttpClient.Create(topology.GameApiAHttpBaseUrl).Build();
        using var apiB = ZLinkHttpClient.Create(topology.GameApiBHttpBaseUrl).Build();

        await apiAStream.Connect.Async(cancellationToken);
        var joined = await apiAStream.Request(new JoinSessionReq("player-alice"))
            .Async<JoinSessionRes>(cancellationToken);
        Ensure(joined.ActiveQuests.Length == 0);

        var firstProgress = apiAStream.WaitFor<QuestProgressNotify>().Async(cancellationToken);
        var firstKill = await apiAStream.Request(new KillMonsterReq("player-alice", "wolf", "forest", "kill-1"))
            .Async<KillMonsterRes>(cancellationToken);
        Ensure(firstKill.EventId == "player-alice-kill-1");
        var firstProgressPush = await firstProgress;
        Ensure(firstProgressPush.Payload.PlayerId == "player-alice");
        Ensure(firstProgressPush.Payload.Progress.QuestId == QuestIds.FirstHunt);
        Ensure(firstProgressPush.Payload.Progress.CurrentCount == 1);

        var completeFirstHunt = apiAStream.WaitFor<QuestCompletedNotify>().Async(cancellationToken);
        _ = await apiAStream.Request(new KillMonsterReq("player-alice", "wolf", "forest", "kill-2"))
            .Async<KillMonsterRes>(cancellationToken);
        var thirdKill = await apiAStream.Request(new KillMonsterReq("player-alice", "wolf", "forest", "kill-3"))
            .Async<KillMonsterRes>(cancellationToken);
        Ensure(thirdKill.EventId == "player-alice-kill-3");
        var completeFirstHuntPush = await completeFirstHunt;
        Ensure(completeFirstHuntPush.Payload.PlayerId == "player-alice");
        Ensure(completeFirstHuntPush.Payload.Progress.QuestId == QuestIds.FirstHunt);
        Ensure(completeFirstHuntPush.Payload.RewardGranted);

        var duplicate = await apiAStream.Request(new KillMonsterReq("player-alice", "wolf", "forest", "kill-3"))
            .Async<KillMonsterRes>(cancellationToken);
        Ensure(duplicate.EventId == thirdKill.EventId);

        var auctionComplete = apiAStream.WaitFor<QuestCompletedNotify>().Async(cancellationToken);
        var auction = await apiAStream.Request(new UnlockFeatureReq("player-alice", "auction", "unlock-auction"))
            .Async<UnlockFeatureRes>(cancellationToken);
        Ensure(auction.EventId == "player-alice-unlock-auction");
        var auctionCompletePush = await auctionComplete;
        Ensure(auctionCompletePush.Payload.PlayerId == "player-alice");
        Ensure(auctionCompletePush.Payload.Progress.QuestId == QuestIds.OpenAuction);
        Ensure(auctionCompletePush.Payload.RewardGranted);
        using var mission = ZLinkHttpClient.Create(topology.MissionAHttpBaseUrl).Build();
        var closeOwner = await mission.Post("/self-check/owner/player-alice/close")
            .AsyncRaw(cancellationToken);
        Ensure(closeOwner.Status is >= 200 and < 300);

        var tutorial = await apiAStream.Request(new CompleteMissionReq("player-alice", "tutorial", "mission-tutorial"))
            .Async<CompleteMissionRes>(cancellationToken);
        Ensure(tutorial.EventId == "player-alice-mission-tutorial");

        var ruins = await apiAStream.Request(new EnterAreaReq("player-alice", "ruins", "enter-ruins"))
            .Async<EnterAreaRes>(cancellationToken);
        Ensure(ruins.EventId == "player-alice-enter-ruins");

        // Simulate an authoritative server event while Bob has no bound session.
        var offlineItem = apiA.Post("/self-check/gameplay/collect/player-bob/healing-herb/1/herb-1")
            .Async<CollectItemRes>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(offlineItem.EventId == "player-bob-herb-1");

        await apiBStream.Connect.Async(cancellationToken);
        var bobJoined = await apiBStream.Request(new JoinSessionReq("player-bob"))
            .Async<JoinSessionRes>(cancellationToken);
        var bobProgress = bobJoined.ActiveQuests.Any(p =>
            p is { QuestId: QuestIds.HerbGathering, CurrentCount: 1 })
            ? bobJoined.ActiveQuests
            : await WaitForStreamProjectionAsync(
                apiBStream,
                "player-bob",
                progress => progress is { QuestId: QuestIds.HerbGathering, CurrentCount: 1 },
                cancellationToken);
        Ensure(bobProgress.Any(p => p is { QuestId: QuestIds.HerbGathering, CurrentCount: 1 }));
        var herbCompletedOnReconnectedStream = apiBStream.WaitFor<QuestCompletedNotify>().Async(cancellationToken);
        var onlineItem = await apiBStream.Request(new CollectItemReq("player-bob", "healing-herb", 4, "herb-2"))
            .Async<CollectItemRes>(cancellationToken);
        Ensure(onlineItem.EventId == "player-bob-herb-2");
        var herbCompletedOnReconnectedStreamPush = await herbCompletedOnReconnectedStream;
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.PlayerId == "player-bob");
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.Progress.QuestId == QuestIds.HerbGathering);
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.RewardGranted);
        Ensure(herbCompletedOnReconnectedStreamPush.Payload.Progress.Status == QuestStatuses.RewardGranted);

        var deleteBobProjection = await apiA.Post($"/self-check/projection/player-bob/{QuestIds.HerbGathering}/delete")
            .AsyncRaw(cancellationToken);
        Ensure(deleteBobProjection.Status is >= 200 and < 300);
        var missingProjection = await GetStreamProjectionAsync(apiBStream, "player-bob", cancellationToken);
        Ensure(missingProjection.All(progress => progress.QuestId != QuestIds.HerbGathering));
        var rebuilt = apiA.Post($"/self-check/projection/player-bob/{QuestIds.HerbGathering}/rebuild")
            .Async<QuestProgress>().AsTask().GetAwaiter().GetResult().Body;
        Ensure(rebuilt is { QuestId: QuestIds.HerbGathering, Status: QuestStatuses.RewardGranted });
        var rebuiltProjection = await GetStreamProjectionAsync(apiBStream, "player-bob", cancellationToken);
        Ensure(rebuiltProjection.Any(progress =>
            progress is { QuestId: QuestIds.HerbGathering, Status: QuestStatuses.RewardGranted }));

        var killWithoutPublish = await apiB.Post("/self-check/gameplay/kill-without-publish/player-alice")
            .AsyncRaw(cancellationToken);
        Ensure(killWithoutPublish.Status is >= 200 and < 300);
        var sync = await apiAStream.Request(new SyncQuestProgressReq("player-alice"))
            .Async<SyncQuestProgressRes>(cancellationToken);
        Ensure(sync.UpdatedQuests.Any(progress =>
            progress.QuestId == QuestIds.FirstHunt && progress.CurrentCount >= 4));
        var reconciled = await WaitForProjectionAsync(
            apiB,
            "player-alice",
            progress => progress.QuestId == QuestIds.FirstHunt && progress.CurrentCount >= 4,
            cancellationToken);
        Ensure(reconciled.Any(p => p.QuestId == QuestIds.FirstHunt && p.CurrentCount >= 4));

        await apiAStream.DisposeAsync();

        var assertion = await WaitForServerAssertionAsync(apiA, cancellationToken);
        Ensure(assertion.Passed);
    }

    private async ValueTask<ServerAssertionResponse> WaitForServerAssertionAsync(
        ZLinkHttpClient api,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + SampleNames.RequestTimeout;
        ServerAssertionResponse? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            last = api.Post("/self-check/assert").Async<ServerAssertionResponse>().AsTask().GetAwaiter().GetResult().Body;
            if (last.Passed) return last;

            await Task.Delay(50, cancellationToken);
        }

        return last ?? throw new InvalidOperationException("Server assertion did not return evidence.");
    }

    private async ValueTask<QuestProgress[]> WaitForProjectionAsync(
        ZLinkHttpClient api,
        string playerId,
        Func<QuestProgress, bool> predicate,
        CancellationToken cancellationToken)
    {
        var deadline = DateTimeOffset.UtcNow + SampleNames.RequestTimeout;
        while (DateTimeOffset.UtcNow < deadline)
        {
            var response = api.Get($"/quest/progress/{playerId}").Async<GetQuestProgressRes>().AsTask().GetAwaiter().GetResult().Body;
            if (response.ActiveQuests.Any(predicate)) return response.ActiveQuests;

            await Task.Delay(50, cancellationToken);
        }

        return api.Get($"/quest/progress/{playerId}").Async<GetQuestProgressRes>().AsTask().GetAwaiter().GetResult().Body.ActiveQuests;
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
            var response = await connector.Request(new GetQuestProgressReq(playerId))
                .Async<GetQuestProgressRes>(cancellationToken);
            if (response.ActiveQuests.Any(predicate)) return response.ActiveQuests;

            await Task.Delay(50, cancellationToken);
        }

        return (await connector.Request(new GetQuestProgressReq(playerId))
            .Async<GetQuestProgressRes>(cancellationToken)).ActiveQuests;
    }

    private static async ValueTask<QuestProgress[]> GetStreamProjectionAsync(
        IZlinkStreamConnector connector,
        string playerId,
        CancellationToken cancellationToken)
    {
        return (await connector.Request(new GetQuestProgressReq(playerId))
            .Async<GetQuestProgressRes>(cancellationToken)).ActiveQuests;
    }

    private static void Ensure(
        bool condition,
        [CallerArgumentExpression(nameof(condition))]
        string? expression = null)
    {
        if (!condition) throw new InvalidOperationException($"Ensure failed: {expression}");
    }
}

internal sealed record ServerAssertionResponse(bool Passed, string[] Evidence);
