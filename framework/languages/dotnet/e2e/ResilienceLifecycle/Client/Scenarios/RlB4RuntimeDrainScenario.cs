// Verifies RL-B4 Runtime Drain behavior.
using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B4 verifies runtime drain and later restore for the provider path.
internal static class RlB4RuntimeDrainScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerB.Post("/admin/drain").AsyncRaw();
        await WaitForWeightAsync(providerB, 0);
        await ProviderTrafficProbe.WaitUntilProviderExcludedAsync(
            consumer, "api-b", "rl-b4-propagation", "RL-B4");
        var retainedRows = (await consumer.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1, ExpectedWeight: 0))
            .Async<TopologyEntryRes[]>()).Body;
        ZlinkStreamAssert.Ensure(
            retainedRows.Length >= 1,
            "RL-B4 runtime drain removed api-b's peer row instead of retaining it.");
        ZlinkStreamAssert.Ensure(
            retainedRows.All(row => row.Weight == 0),
            "RL-B4 runtime drain did not publish api-b's zero weight.");
        var beforeDrain = (await providerB.Get("/evidence").Async<string[]>()).Body;

        for (var i = 0; i < 20; i++)
        {
            var marker = $"rl-b4-drained-{i}";
            var reply = await ProviderTrafficProbe.RequestWithoutRetryAsync(
                consumer,
                new ProfileReq("fast", marker));
            ZlinkStreamAssert.Ensure(reply.ProviderRid == "api-a", "RL-B4 drained api-b received a new request.");
        }

        var afterDrain = (await providerB.Get("/evidence").Async<string[]>()).Body;
        ZlinkStreamAssert.Ensure(
            CountNew(afterDrain, beforeDrain, "profile-request|rid=api-b|marker=rl-b4-drained-") == 0,
            "RL-B4 api-b evidence changed after drain.");

        await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["profile-request|rid=api-a|marker=rl-b4-drained-"], []))
            .Async<string[]>();

        await providerB.Post("/admin/restore").AsyncRaw();
        await WaitForWeightAsync(providerB, 100);

        await ProviderTrafficProbe.DriveUntilProviderServesAsync(
            consumer, providerB, "rl-b4-restored", "RL-B4");

        Console.WriteLine("scenario RL-B4 passed");
    }

    private static async Task WaitForWeightAsync(ZLinkHttpClient provider, int expected)
    {
        await provider.Post("/admin/weight/wait")
            .Body(new WeightWaitReq(expected))
            .AsyncRaw();
    }

    private static int CountNew(string[] after, string[] before, string pattern)
    {
        return Math.Max(0, after.Count(line => line.Contains(pattern, StringComparison.Ordinal))
                           - before.Count(line => line.Contains(pattern, StringComparison.Ordinal)));
    }
}
