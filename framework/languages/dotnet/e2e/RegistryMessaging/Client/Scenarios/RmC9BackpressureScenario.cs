using Zlink.HttpClient;
using RegistryMessaging.Client;
using RegistryMessaging.Shared;

namespace RegistryMessaging.Client.Scenarios;

// RM-C9 verifies that low high-water marks produce bounded send failures under
// pressure and that the channel recovers for a later request.
internal static class RmC9BackpressureScenario
{
    public static async Task RunAsync(ZLinkHttpClient backpressureConsumer, ZLinkHttpClient providerA)
    {
        await backpressureConsumer.Post("/profile/backpressure/reset").SubmitAsync<object>();
        var marker = $"rm-c9-{Guid.NewGuid():N}";
        var outcomes = await Task.WhenAll(Enumerable.Range(0, 32)
            .Select(index => SendBackpressureCommandAsync(
                backpressureConsumer,
                $"rm-c9-slow-{marker}-{index}")));
        ScenarioAssert.That(
            outcomes.Any(outcome => outcome == "BoundedFailure"),
            "RM-C9 expected at least one bounded failure while the low-HWM socket was saturated.");

        await Task.Delay(TimeSpan.FromSeconds(5));
        var followUp = (await backpressureConsumer.Post("/profile/request")
            .Body(new ProfileRequest("rm-c9-after"))
            .SubmitAsync<ProfileReply>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:rm-c9-after", "RM-C9 follow-up request failed after backlog cleared.");

        var evidence = (await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitRequest("rm-c9-after", TimeoutMilliseconds: 20000))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("rm-c9-after", StringComparison.Ordinal)),
            "RM-C9 recovery evidence missing.");
        Console.WriteLine("scenario RM-C9 passed");
    }

    static async Task<string> SendBackpressureCommandAsync(ZLinkHttpClient backpressureConsumer, string commandId)
    {
        return (await backpressureConsumer.Post("/profile/backpressure/send")
            .Body(new ProfileCommand(commandId))
            .SubmitAsync<string>()).Body;
    }
}
