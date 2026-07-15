// Verifies RL-A1 Provider Restart behavior.
using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-A1 verifies recovery when the same provider endpoint is restarted.
internal static class RlA1ProviderRestartScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/shutdown").AsyncRaw();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").AsyncRaw();
                if (health.Status != 200) break;
            }
            catch
            {
                break;
            }

            await Task.Delay(100);
        }

        await processes.WaitInitialProviderBExitedAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 0))
            .Async<TopologyEntryRes[]>();

        var survivingReplies = 0;
        var downWindowFailures = 0;
        for (var i = 0; i < 12; i++)
        {
            var marker = $"rl-a1-down-{i}";
            var attempt = (await consumer.Post("/profile/request/attempt/1000")
                .Body(new ProfileReq("fast", marker))
                .Async<ProfileAttemptRes>()).Body;
            if (attempt.Reply is { } reply)
            {
                ZlinkStreamAssert.Ensure(reply.ProviderRid == "api-a",
                    "RL-A1 request during api-b restart used the stopped provider.");
                survivingReplies++;
                continue;
            }

            ZlinkStreamAssert.Ensure(
                attempt.IsRetriable
                && attempt.ErrorKind is nameof(TimeoutException)
                    or "RouteNotConnected"
                    or "RequestTargetNotFound"
                    or "RequestFailed",
                $"RL-A1 returned unexpected down-window error '{attempt.ErrorKind}'.");
            downWindowFailures++;
        }
        ZlinkStreamAssert.Ensure(survivingReplies > 0,
            "RL-A1 did not route any down-window request to the surviving provider.");
        ZlinkStreamAssert.Ensure(survivingReplies + downWindowFailures == 12,
            "RL-A1 did not classify every down-window request result.");

        await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["marker=rl-a1-down-"], []))
            .Async<string[]>();

        await processes.StartProviderBAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .Async<TopologyEntryRes[]>();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").AsyncRaw();
                if (health.Status == 200) break;
            }
            catch
            {
                // The scenario keeps polling until the restarted provider accepts HTTP traffic.
            }

            await Task.Delay(100);
        }

        await ProviderTrafficProbe.DriveUntilProviderServesAsync(
            consumer, providerB, "rl-a1-restored", "RL-A1");

        Console.WriteLine("scenario RL-A1 passed");
    }
}
