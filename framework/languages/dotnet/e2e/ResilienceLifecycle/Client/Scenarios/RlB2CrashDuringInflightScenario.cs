using ResilienceLifecycle.Client.Support;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B2 verifies public failure for in-flight work when a provider crashes.
internal static class RlB2CrashDuringInflightScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ZLinkHttpClient registry,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerA.Post("/admin/drain").SubmitRawAsync();
        await providerA.Post("/admin/weight/wait")
            .Body(new WeightWaitReq(0))
            .SubmitRawAsync();

        var marker = $"rl-b2-slow-{Guid.NewGuid():N}";
        var inFlight = consumer.Post("/profile/request")
            .Body(new ProfileReq("slow", marker))
            .SubmitAsync<ProfileRes>();

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq([$"profile-start|rid=api-b|marker={marker}"], []))
            .SubmitAsync<string[]>();

        await providerB.Post("/admin/crash").SubmitRawAsync();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").SubmitRawAsync();
                if (health.Status != 200) break;
            }
            catch
            {
                break;
            }

            await Task.Delay(100);
        }

        var failed = false;
        try
        {
            await inFlight;
        }
        catch
        {
            failed = true;
        }

        ScenarioAssert.That(failed, "RL-B2 in-flight request unexpectedly completed after provider crash.");

        await providerA.Post("/admin/restore").SubmitRawAsync();
        await providerA.Post("/admin/weight/wait")
            .Body(new WeightWaitReq(100))
            .SubmitRawAsync();

        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 0))
            .SubmitAsync<TopologyEntryRes[]>();
        var followUp = (await consumer.Post("/profile/request/new-client")
            .Body(new ProfileReq("fast", "rl-b2-after-crash"))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(followUp.ProviderRid == "api-a", "RL-B2 surviving provider traffic failed.");

        await processes.StartProviderBAsync();
        await registry.Post("/topology/wait")
            .Body(new TopologyWaitReq("api-b", "Ready", 1))
            .SubmitAsync<TopologyEntryRes[]>();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").SubmitRawAsync();
                if (health.Status == 200) break;
            }
            catch
            {
                // The scenario keeps polling until api-b accepts HTTP traffic again.
            }

            await Task.Delay(100);
        }

        for (var i = 0; i < 32; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileReq("fast", $"rl-b2-restored-{i}"))
                .SubmitAsync<ProfileRes>()).Body;
            ScenarioAssert.That(reply.Value == "profile:fast", "RL-B2 restored request returned an unexpected value.");
        }

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitReq(["marker=rl-b2-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-B2 passed");
    }
}