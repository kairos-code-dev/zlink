// Verifies RM-B2 Scale In behavior.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-B2 verifies that after a graceful scale-in the stopped provider's peer
// location row is removed on the shutdown path (no owner lease expiry wait)
// and traffic continues through the remaining provider only.
internal static class RmB2ScaleInScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-b2");
        var providerA = await cluster.StartProviderAsync("api-a", "api-a");
        var providerB = await cluster.StartProviderAsync("api-b", "api-b");
        var consumer = await cluster.StartConsumerAsync("consumer");
        using var requester = ZLinkHttpClient.Create(consumer.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();
        using var providerAClient = ZLinkHttpClient.Create(providerA.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();
        using var providerBClient = ZLinkHttpClient.Create(providerB.HttpUrl)
            .Timeout(TimeSpan.FromMinutes(5))
            .Build();

        var beforeA = await ReadEvidenceAsync(providerAClient);
        var beforeB = await ReadEvidenceAsync(providerBClient);

        await WaitForPeerRowAsync(requester, "api-b", expected: true);
        await WaitConnectionEvidenceAsync(
            requester,
            $"monitor-socket|source=profile.client|kind=ConnectionReady|remote={providerA.ChannelEndpoint}");
        await WaitConnectionEvidenceAsync(
            requester,
            $"monitor-socket|source=profile.client|kind=ConnectionReady|remote={providerB.ChannelEndpoint}");
        var firstBefore = (await requester.Post("/profile/request")
            .Body(new ProfileReq("rm-b2-first-before-scale-in"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(
            firstBefore.ProviderRid is "api-a" or "api-b"
            && firstBefore.Value == "profile:rm-b2-first-before-scale-in",
            "RM-B2 first request after peer convergence failed.");
        var markerBefore = $"rm-b2-before-{Guid.NewGuid():N}";
        var valuesBefore = Enumerable.Range(0, 40)
            .Select(index => $"{markerBefore}-{index}")
            .ToArray();
        var repliesBefore = new List<ProfileRes>(valuesBefore.Length);
        foreach (var value in valuesBefore)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body;
            repliesBefore.Add(reply);
        }

        ZlinkStreamAssert.Ensure(repliesBefore.Count == valuesBefore.Length, "RM-B2 pre-scale reply count mismatch.");
        ZlinkStreamAssert.Ensure(
            repliesBefore.All(reply => reply.ProviderRid is "api-a" or "api-b"),
            "RM-B2 reply provider mismatch before scale-in.");

        var apiABeforeValues = valuesBefore.Zip(repliesBefore)
            .Where(result => result.Second.ProviderRid == "api-a")
            .Select(result => result.First)
            .ToArray();
        var apiBBeforeValues = valuesBefore.Zip(repliesBefore)
            .Where(result => result.Second.ProviderRid == "api-b")
            .Select(result => result.First)
            .ToArray();
        ZlinkStreamAssert.Ensure(apiABeforeValues.Length > 0 && apiBBeforeValues.Length > 0,
            "RM-B2 expected both providers before scale-in.");
        var scaleOutA = await WaitEvidenceAsync(providerAClient, apiABeforeValues[^1]);
        var scaleOutB = await WaitEvidenceAsync(providerBClient, apiBBeforeValues[^1]);
        var preA = EvidenceDelta.CountMatching(scaleOutA, beforeA, "profile-request|rid=api-a", markerBefore);
        var preB = EvidenceDelta.CountMatching(scaleOutB, beforeB, "profile-request|rid=api-b", markerBefore);
        ZlinkStreamAssert.Ensure(preA == apiABeforeValues.Length && preB == apiBBeforeValues.Length
                                                            && preA + preB == valuesBefore.Length,
            "RM-B2 expected both providers before scale-in.");

        var beforeDisconnect = await WaitConnectionEvidenceAsync(
            requester,
            $"monitor-socket|source=profile.client|kind=ConnectionReady|remote={providerB.ChannelEndpoint}");

        var transitionMarker = $"rm-b2-transition-{Guid.NewGuid():N}";
        var transitionTasks = Enumerable.Range(0, 16)
            .Select(index => ObserveTransitionRequestAsync(requester, $"{transitionMarker}-{index}"))
            .ToArray();
        await WaitEvidenceAsync(providerAClient, $"profile-request-start|rid=api-a|value={transitionMarker}");
        await WaitEvidenceAsync(providerBClient, $"profile-request-start|rid=api-b|value={transitionMarker}");
        var traffic = new TransitionTrafficGate();
        var continuingResults = new ConcurrentBag<TransitionResult>();
        var continuingTraffic = Enumerable.Range(0, 4)
            .Select(worker => RunTransitionTrafficAsync(
                requester,
                transitionMarker,
                worker,
                traffic,
                continuingResults))
            .ToArray();

        var stopProviderB = cluster.StopAsync(providerB);
        await WaitForPeerWeightAsync(requester, "api-b", 0);
        await WaitConnectionEvidenceAsync(
            requester,
            $"monitor-socket|source=profile.client|kind=ConnectionReady|remote={providerB.ChannelEndpoint}"
            + "|routing=api-b",
            beforeDisconnect.Length);

        // The row and socket event are only convergence triggers. These requests prove that
        // the asynchronous native weight update reached this connected peer; none may still
        // select api-b.
        await ConfirmWeightPropagationWithTrafficAsync(requester);

        await stopProviderB;
        traffic.Stop();

        var transitionResults = await Task.WhenAll(transitionTasks);
        await Task.WhenAll(continuingTraffic);
        ZlinkStreamAssert.Ensure(
            transitionResults.Concat(continuingResults).All(result => result is
                { Reply: { ProviderRid: "api-a" or "api-b" }, ErrorKind: null }
                or { Reply: null, ErrorKind: ZLinkFrameworkErrorKind.RequestFailed }),
            "RM-B2 in-flight request did not complete with a provider reply or RequestFailed.");
        ZlinkStreamAssert.Ensure(continuingResults.Count >= 4,
            "RM-B2 did not keep request traffic active throughout provider drain.");

        // Graceful shutdown must remove api-b's peer row from the runtime
        // query peer list without waiting for owner lease expiry (doc RM-B2).
        await WaitForPeerRowGoneAsync(requester, "api-b");
        await WaitConnectionEvidenceAsync(
            requester,
            $"monitor-socket|source=profile.client|kind=Disconnected|remote={providerB.ChannelEndpoint}",
            beforeDisconnect.Length);
        await WaitConnectionEvidenceAsync(
            requester,
            $"monitor-socket|source=profile.client|kind=ConnectionReady|remote={providerB.ChannelEndpoint}"
            + "|routing=api-b",
            beforeDisconnect.Length);

        // NativeValue is native event diagnostic data, not an aggregate ready-pipe count.
        // The two public transitions can be dispatched in either order during pipe termination;
        // observing both after the baseline proves the ready-set update without interpreting the
        // diagnostic value. The first single request then proves that the terminated pipe no
        // longer participates in routing.
        var firstAfter = (await requester.Post("/profile/request")
            .Body(new ProfileReq("rm-b2-first-after-scale-in"))
            .Async<ProfileRes>()).Body;
        ZlinkStreamAssert.Ensure(
            firstAfter.ProviderRid == "api-a"
            && firstAfter.Value == "profile:rm-b2-first-after-scale-in",
            "RM-B2 first post-scale request did not succeed on api-a.");

        beforeA = await ReadEvidenceAsync(providerAClient);
        var markerAfter = $"rm-b2-after-{Guid.NewGuid():N}";
        var valuesAfter = Enumerable.Range(0, 20)
            .Select(index => $"{markerAfter}-{index}")
            .ToArray();
        var repliesAfter = new List<ProfileRes>(valuesAfter.Length);
        foreach (var value in valuesAfter)
        {
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body;
            repliesAfter.Add(reply);
        }

        ZlinkStreamAssert.Ensure(repliesAfter.Count == valuesAfter.Length, "RM-B2 post-scale reply count mismatch.");
        ZlinkStreamAssert.Ensure(
            repliesAfter.All(reply => reply.ProviderRid == "api-a"),
            "RM-B2 after scale-in should reach api-a only.");

        var afterA = await WaitEvidenceAsync(providerAClient, valuesAfter[^1]);
        var a = EvidenceDelta.CountMatching(afterA, beforeA, "profile-request|rid=api-a", markerAfter);
        ZlinkStreamAssert.Ensure(a == valuesAfter.Length, "RM-B2 expected only api-a after scale-in.");
    }

    private static Task WaitForPeerRowGoneAsync(ZLinkHttpClient http, string rid)
        => WaitForPeerRowAsync(http, rid, expected: false);

    private static async Task WaitForPeerRowAsync(ZLinkHttpClient http, string rid, bool expected)
    {
        await http.Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq("profile", "Router", rid, expected))
            .Async<PeerLocationRow[]>();
    }

    private static async Task WaitForPeerWeightAsync(ZLinkHttpClient http, string rid, uint weight)
    {
        await http.Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq("profile", "Router", rid, Present: true, Weight: weight))
            .Async<PeerLocationRow[]>();
    }

    private static async Task ConfirmWeightPropagationWithTrafficAsync(ZLinkHttpClient http)
    {
        var consecutiveApiAReplies = 0;
        var sequence = 0;
        while (consecutiveApiAReplies < 16 && sequence < 64)
        {
            try
            {
                var value = $"rm-b2-after-weight-zero-{sequence++}";
                var reply = (await http.Post("/profile/request")
                    .Body(new ProfileReq(value))
                    .Async<ProfileRes>()).Body;
                ZlinkStreamAssert.Ensure(
                    reply.ProviderRid == "api-a" && reply.Value == $"profile:{value}",
                    "RM-B2 actual traffic reached api-b after its weight-zero row was observed.");
                consecutiveApiAReplies++;
            }
            catch (ZLinkFrameworkException error) when (
                error.Kind == ZLinkFrameworkErrorKind.RequestFailed)
            {
                consecutiveApiAReplies = 0;
            }
        }

        ZlinkStreamAssert.Ensure(
            consecutiveApiAReplies == 16,
            "RM-B2 weight propagation did not produce 16 consecutive api-a replies.");
    }

    private static async Task<string[]> ReadEvidenceAsync(ZLinkHttpClient http)
    {
        return (await http.Get("/evidence").Async<string[]>()).Body;
    }

    private static async Task<string[]> WaitEvidenceAsync(ZLinkHttpClient http, string contains)
    {
        return (await http.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(contains))
            .Async<string[]>()).Body;
    }

    private static async Task<string[]> WaitConnectionEvidenceAsync(
        ZLinkHttpClient http,
        string contains,
        int afterCount = 0)
    {
        return (await http.Post("/connections/wait")
            .Body(new EvidenceWaitReq(contains, AfterCount: afterCount))
            .Async<string[]>()).Body;
    }

    private static async Task<TransitionResult> ObserveTransitionRequestAsync(
        ZLinkHttpClient http,
        string value)
    {
        try
        {
            var reply = (await http.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(reply.Value == $"profile:{value}",
                "RM-B2 transition reply payload mismatch.");
            return new TransitionResult(reply, null);
        }
        catch (ZLinkFrameworkException error) when (
            error.Kind == ZLinkFrameworkErrorKind.RequestFailed)
        {
            return new TransitionResult(null, error.Kind);
        }
    }

    private static async Task RunTransitionTrafficAsync(
        ZLinkHttpClient http,
        string marker,
        int worker,
        TransitionTrafficGate gate,
        ConcurrentBag<TransitionResult> results)
    {
        var sequence = 0;
        while (!gate.IsStopped)
        {
            results.Add(await ObserveTransitionRequestAsync(
                http,
                $"{marker}-continuous-{worker}-{sequence++}"));
        }
    }

    private sealed record TransitionResult(
        ProfileRes? Reply,
        ZLinkFrameworkErrorKind? ErrorKind);

    private sealed class TransitionTrafficGate
    {
        private int _stopped;
        public bool IsStopped => Volatile.Read(ref _stopped) != 0;
        public void Stop() => Volatile.Write(ref _stopped, 1);
    }
}
