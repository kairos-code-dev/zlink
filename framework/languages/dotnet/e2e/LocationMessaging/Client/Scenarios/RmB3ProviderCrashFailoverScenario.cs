// Verifies RM-B3 provider crash failover while stale location rows remain visible.
using LocationMessaging.Client.Support;
using LocationMessaging.Shared;
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
        var providerB = await cluster.StartProviderAsync(
            "api-b",
            "api-b",
            routePeers: [providerA.RouteEndpoint]);
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
        var inFlight = Enumerable.Range(0, 4)
            .Select(index =>
            {
                var value = $"{marker}-{index}";
                return (Value: value, Task: ObserveRequestAsync(requester, value));
            })
            .ToArray();
        var startedOnA = (await providerAClient.Post("/evidence/wait")
            .Body(new EvidenceWaitReq($"profile-request-start|rid=api-a|value={marker}"))
            .Async<string[]>()).Body;
        var startedValue = startedOnA
            .Select(line => TryReadValue(line, $"profile-request-start|rid=api-a|value={marker}"))
            .First(value => value is not null)!;

        await cluster.CrashAsync(providerA);
        var continuingMarker = $"rm-b3-continuing-{Guid.NewGuid():N}";
        var continuingTraffic = Enumerable.Range(0, 20)
            .Select(index => ObserveRequestAsync(requester, $"{continuingMarker}-{index}"))
            .ToArray();

        var transition = await Task.WhenAll(inFlight.Select(request => request.Task));
        ZlinkStreamAssert.Ensure(
            transition.All(result => result.Outcome is "api-a" or "api-b"
                or nameof(ZLinkFrameworkErrorKind.RouteNotConnected)
                or "Timeout"),
            "RM-B3 in-flight request did not finish with a reply or bounded public request error.");
        var crashedRequest = transition.Single(result => result.Value == startedValue);
        ZlinkStreamAssert.Ensure(
            crashedRequest.Outcome is nameof(ZLinkFrameworkErrorKind.RouteNotConnected) or "Timeout",
            $"RM-B3 request started on crashed api-a completed as '{crashedRequest.Outcome}'.");

        var continuingOutcomes = await Task.WhenAll(continuingTraffic);
        ZlinkStreamAssert.Ensure(
            continuingOutcomes.Any(result => result.Outcome == "api-b"),
            "RM-B3 did not keep serving new untargeted traffic on the remaining provider after crash.");
        await WaitForPeerAsync(requester, "api-a", present: true);
        ZlinkStreamAssert.Ensure(
            continuingOutcomes.All(result => result.Outcome is "api-b"
                or nameof(ZLinkFrameworkErrorKind.RouteNotConnected)
                or "Timeout"),
            "RM-B3 continuing request completed with an outcome outside the public failover contract.");

        await WaitForPeerAsync(requester, "api-a", present: false);
        await WaitForOnlyRemainingProviderAsync(requester);
        for (var index = 0; index < 20; index++)
        {
            var value = $"rm-b3-after-{index}";
            var reply = await ObserveRequestAsync(requester, value);
            ZlinkStreamAssert.Ensure(
                reply.Outcome == "api-b",
                "RM-B3 post-expiry request did not use the remaining provider.");
        }

        var targeted = (await providerBClient.Post("/profile/route/target")
            .Body(new TargetedRoutePing("api-a", "rm-b3-target-dead"))
            .Async<ExpectedFailureRes>()).Body;
        ZlinkStreamAssert.Ensure(
            targeted.ErrorKind == nameof(ZLinkFrameworkErrorKind.RouteNotConnected),
            "RM-B3 known but disconnected api-a did not report RouteNotConnected.");
        var missing = (await providerBClient.Post("/profile/route/target")
            .Body(new TargetedRoutePing("api-missing", "rm-b3-target-missing"))
            .Async<ExpectedFailureRes>()).Body;
        ZlinkStreamAssert.Ensure(
            missing.ErrorKind == nameof(ZLinkFrameworkErrorKind.RequestTargetNotFound),
            "RM-B3 unknown route target did not report RequestTargetNotFound.");
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

    private static async Task<RequestOutcome> ObserveRequestAsync(ZLinkHttpClient requester, string value)
    {
        var result = (await requester.Post("/profile/request/outcome")
            .Body(new ProfileReq(value))
            .Async<RequestOutcomeRes>()).Body;
        return new RequestOutcome(result.Value, result.Outcome);
    }

    private static string? TryReadValue(string line, string prefix)
        => line.Contains(prefix, StringComparison.Ordinal)
            ? line[(line.IndexOf("|value=", StringComparison.Ordinal) + "|value=".Length)..]
            : null;

    private static async Task WaitForOnlyRemainingProviderAsync(ZLinkHttpClient requester)
    {
        var consecutive = 0;
        for (var attempt = 0; attempt < 20 && consecutive < 2; attempt++)
        {
            var outcome = await ObserveRequestAsync(requester, $"rm-b3-ready-{attempt}");
            consecutive = outcome.Outcome == "api-b" ? consecutive + 1 : 0;
        }

        ZlinkStreamAssert.Ensure(
            consecutive == 2,
            "RM-B3 target-free messaging did not converge to the remaining provider after lease expiry.");
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

    private sealed record RequestOutcome(string Value, string Outcome);
}
