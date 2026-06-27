using ResilienceLifecycle.Client;
using ResilienceLifecycle.Shared;
using Zlink.HttpClient;

namespace ResilienceLifecycle.Client.Scenarios;

// RL-B2 verifies public failure for in-flight work when a provider crashes.
internal static class RlB2CrashDuringInflightScenario
{
    public static async Task RunAsync(
        ZLinkHttpClient consumer,
        ResilienceProcessManager processes,
        ZLinkHttpClient providerA,
        ZLinkHttpClient providerB)
    {
        await providerA.Post("/admin/drain").SubmitRawAsync();
        await providerA.Post("/admin/weight/wait")
            .Body(new WeightWaitRequest(0))
            .SubmitRawAsync();

        var marker = $"rl-b2-slow-{Guid.NewGuid():N}";
        var inFlight = consumer.Post("/profile/request")
            .Body(new ProfileRequest("slow", marker))
            .SubmitAsync<ProfileReply>();

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest([$"profile-start|rid=api-b|marker={marker}"], []))
            .SubmitAsync<string[]>();

        await providerB.Post("/admin/crash").SubmitRawAsync();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").SubmitRawAsync();
                if (health.Status != 200)
                {
                    break;
                }
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
            .Body(new WeightWaitRequest(100))
            .SubmitRawAsync();

        ProfileReply? followUp = null;
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                followUp = (await consumer.Post("/profile/request")
                    .Body(new ProfileRequest("fast", $"rl-b2-after-crash-{attempt}"))
                    .SubmitAsync<ProfileReply>()).Body;
                if (followUp.ProviderRid == "api-a")
                {
                    break;
                }
            }
            catch
            {
                // The scenario keeps issuing real requests until routing converges to api-a.
            }

            await Task.Delay(100);
        }

        ScenarioAssert.That(followUp?.ProviderRid == "api-a", "RL-B2 surviving provider traffic failed.");

        await processes.StartProviderBAsync();
        for (var attempt = 0; attempt < 100; attempt++)
        {
            try
            {
                var health = await providerB.Get("/health").SubmitRawAsync();
                if (health.Status == 200)
                {
                    break;
                }
            }
            catch
            {
                // The scenario keeps polling until api-b accepts HTTP traffic again.
            }

            await Task.Delay(100);
        }

        var restored = false;
        for (var i = 0; i < 24; i++)
        {
            var reply = (await consumer.Post("/profile/request")
                .Body(new ProfileRequest("fast", $"rl-b2-restored-{i}"))
                .SubmitAsync<ProfileReply>()).Body;
            restored = restored || reply.ProviderRid == "api-b";
        }

        ScenarioAssert.That(restored, "RL-B2 did not route traffic to api-b after restart.");

        await providerB.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest(["marker=rl-b2-restored-"], []))
            .SubmitAsync<string[]>();

        Console.WriteLine("scenario RL-B2 passed");
    }
}
