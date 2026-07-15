// Verifies RM-B3 provider crash failover while stale location rows remain visible.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
using System.Collections.Concurrent;
using Zlink.Framework.Contracts.Errors;
using Zlink.HttpClient;

namespace LocationMessaging.Client.Scenarios;

// RM-B3 proves that a crashed provider is fenced by owner-lease expiry while
// the remaining provider continues serving new untargeted requests.
internal static class RmB3ProviderCrashFailoverScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        await using var cluster = await DynamicClusterLauncher.StartAsync(options, "rm-b3");
        var providerA = await cluster.StartProviderAsync("api-a", "api-a");
        var providerB = await cluster.StartProviderAsync("api-b", "api-b");
        var consumer = await cluster.StartConsumerAsync("consumer");
        using var requester = ZLinkHttpClient.Create(consumer.HttpUrl)
            .Timeout(TimeSpan.FromSeconds(90))
            .Build();
        using var providerAClient = ZLinkHttpClient.Create(providerA.HttpUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();
        using var providerBClient = ZLinkHttpClient.Create(providerB.HttpUrl)
            .Timeout(TimeSpan.FromSeconds(10))
            .Build();

        await WaitForPeerAsync(requester, "api-a", present: true);
        await WaitForPeerAsync(requester, "api-b", present: true);
        await ProveBothProvidersAsync(requester, providerAClient, providerBClient);

        var marker = $"rm-b3-transition-{Guid.NewGuid():N}";
        var inFlight = Enumerable.Range(0, 16)
            .Select(index => ObserveRequestAsync(requester, $"{marker}-{index}"))
            .ToArray();
        await providerAClient.Post("/evidence/wait")
            .Body(new EvidenceWaitReq($"profile-request-start|rid=api-a|value={marker}"))
            .Async<string[]>();

        await cluster.CrashAsync(providerA);
        var continuingMarker = $"rm-b3-continuing-{Guid.NewGuid():N}";
        using var continuingTrafficCancellation = new CancellationTokenSource();
        var continuingOutcomes = new ConcurrentQueue<string>();
        var nextSequence = 0;
        var continuingTraffic = Enumerable.Range(0, 4)
            .Select(_ => SendContinuingTrafficAsync(
                requester,
                continuingMarker,
                continuingOutcomes,
                () => Interlocked.Increment(ref nextSequence),
                continuingTrafficCancellation.Token))
            .ToArray();

        var transition = await Task.WhenAll(inFlight);
        ZlinkStreamAssert.Ensure(
            transition.All(result => result is "api-a" or "api-b" or nameof(ZLinkFrameworkErrorKind.RequestFailed)
                or "Timeout"),
            "RM-B3 in-flight request did not finish with a reply or bounded public request error.");

        var continuingOnB = (await providerBClient.Post("/evidence/wait")
            .Body(new EvidenceWaitReq($"profile-request|rid=api-b|value={continuingMarker}"))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            continuingOnB.Length > 0,
            "RM-B3 did not keep serving new untargeted traffic on the remaining provider after crash.");
        await WaitForPeerAsync(requester, "api-a", present: true);

        await WaitForPeerAsync(requester, "api-a", present: false);
        await continuingTrafficCancellation.CancelAsync();
        await Task.WhenAll(continuingTraffic);
        ZlinkStreamAssert.Ensure(
            continuingOutcomes.Contains("api-b"),
            "RM-B3 observed no successful request on the remaining provider during the stale-row window.");
        ZlinkStreamAssert.Ensure(
            continuingOutcomes.All(result => result is "api-b" or nameof(ZLinkFrameworkErrorKind.RequestFailed)
                or "Timeout"),
            "RM-B3 continuing request completed with an outcome outside the public failover contract.");

        await ConfirmFailoverWithTrafficAsync(requester);
        for (var index = 0; index < 20; index++)
        {
            var value = $"rm-b3-after-{index}";
            var reply = (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body;
            ZlinkStreamAssert.Ensure(
                reply.ProviderRid == "api-b" && reply.Value == $"profile:{value}",
                "RM-B3 post-expiry request did not use the remaining provider.");
        }

        var targeted = (await providerBClient.Post("/profile/route/target")
            .Body(new TargetedRoutePing("api-a", "rm-b3-target-dead"))
            .Async<ExpectedFailureRes>()).Body;
        ZlinkStreamAssert.Ensure(
            targeted.ErrorKind == nameof(ZLinkFrameworkErrorKind.RequestTargetNotFound),
            "RM-B3 targeted request to the expired provider did not report RequestTargetNotFound.");
    }

    private static async Task ConfirmFailoverWithTrafficAsync(ZLinkHttpClient requester)
    {
        var consecutiveApiBReplies = 0;
        for (var sequence = 0; sequence < 64 && consecutiveApiBReplies < 8; sequence++)
        {
            try
            {
                var value = $"rm-b3-converge-{sequence}";
                var reply = (await requester.Post("/profile/request")
                    .Body(new ProfileReq(value))
                    .Async<ProfileRes>()).Body;
                ZlinkStreamAssert.Ensure(
                    reply.ProviderRid == "api-b" && reply.Value == $"profile:{value}",
                    "RM-B3 convergence request used the crashed provider.");
                consecutiveApiBReplies++;
            }
            catch (ZLinkFrameworkException error) when (
                error.Kind == ZLinkFrameworkErrorKind.RequestFailed)
            {
                consecutiveApiBReplies = 0;
            }
        }

        ZlinkStreamAssert.Ensure(
            consecutiveApiBReplies == 8,
            "RM-B3 did not converge to the remaining provider after the crashed row expired.");
    }

    private static async Task ProveBothProvidersAsync(
        ZLinkHttpClient requester,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        var marker = $"rm-b3-before-{Guid.NewGuid():N}";
        for (var index = 0; index < 40; index++)
            _ = await requester.Post("/profile/request")
                .Body(new ProfileReq($"{marker}-{index}"))
                .Async<ProfileRes>();

        var a = (await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq($"profile-request|rid=api-a|value={marker}"))
            .Async<string[]>()).Body;
        var b = (await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq($"profile-request|rid=api-b|value={marker}"))
            .Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(a.Length > 0 && b.Length > 0, "RM-B3 expected both providers before crash.");
    }

    private static async Task<string> ObserveRequestAsync(ZLinkHttpClient requester, string value)
    {
        try
        {
            return (await requester.Post("/profile/request")
                .Body(new ProfileReq(value))
                .Async<ProfileRes>()).Body.ProviderRid;
        }
        catch (ZLinkFrameworkException error) when (
            error.Kind == ZLinkFrameworkErrorKind.RequestFailed)
        {
            return error.Kind.ToString();
        }
        catch (TimeoutException)
        {
            return "Timeout";
        }
    }

    private static async Task SendContinuingTrafficAsync(
        ZLinkHttpClient requester,
        string marker,
        ConcurrentQueue<string> outcomes,
        Func<int> nextSequence,
        CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                var value = $"{marker}-{nextSequence()}";
                var reply = (await requester.Post("/profile/request")
                    .Body(new ProfileReq(value))
                    .Async<ProfileRes>(cancellationToken)).Body;
                outcomes.Enqueue(reply.ProviderRid);
            }
            catch (ZLinkFrameworkException error) when (
                error.Kind == ZLinkFrameworkErrorKind.RequestFailed)
            {
                outcomes.Enqueue(error.Kind.ToString());
            }
            catch (TimeoutException)
            {
                outcomes.Enqueue("Timeout");
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
        }
    }

    private static Task WaitForPeerAsync(ZLinkHttpClient requester, string rid, bool present)
        => requester.Post("/locations/peers/wait")
            .Body(new PeerLocationWaitReq(
                "profile",
                "Router",
                rid,
                present,
                TimeoutMilliseconds: present ? 30000 : 60000))
            .Async<PeerLocationRow[]>()
            .AsTask();
}
