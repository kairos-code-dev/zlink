using RegistryMessaging.Client.Support;
using RegistryMessaging.Shared;
using Zlink.HttpClient;

namespace RegistryMessaging.Client.Scenarios;

// RM-C9 verifies that one-way send pressure does not expose a public send
// completion oracle and that the channel recovers for a later request.
internal static class RmC9BackpressureScenario
{
    private const int SlowSendCount = 8;

    public static async Task RunAsync(ZLinkHttpClient backpressureConsumer, ZLinkHttpClient providerA)
    {
        await backpressureConsumer.Post("/profile/backpressure/reset").SubmitAsync<object>();
        var marker = $"rm-c9-{Guid.NewGuid():N}";
        var outcomes = await Task.WhenAll(Enumerable.Range(0, SlowSendCount)
            .Select(index => SendBackpressureCommandAsync(
                backpressureConsumer,
                $"rm-c9-slow-{marker}-{index}")));
        ScenarioAssert.That(
            outcomes.All(outcome => outcome == "Submitted"),
            "RM-C9 expected all one-way sends to be submitted without a public bounded-failure oracle.");

        await Task.Delay(TimeSpan.FromSeconds(10));
        var followUp = (await backpressureConsumer.Post("/profile/request")
            .Body(new ProfileReq("rm-c9-after"))
            .SubmitAsync<ProfileRes>()).Body;
        ScenarioAssert.That(followUp.Value == "profile:rm-c9-after",
            "RM-C9 follow-up request failed after backlog cleared.");

        var evidence = (await providerA.Post("/evidence/wait")
            .Body(new EvidenceWaitReq("rm-c9-after", 20000))
            .SubmitAsync<string[]>()).Body;
        ScenarioAssert.That(
            evidence.Any(line => line.Contains("rm-c9-after", StringComparison.Ordinal)),
            "RM-C9 recovery evidence missing.");
        Console.WriteLine("scenario RM-C9 passed");
    }

    private static async Task<string> SendBackpressureCommandAsync(ZLinkHttpClient backpressureConsumer,
        string commandId)
    {
        return (await backpressureConsumer.Post("/profile/backpressure/send")
            .Body(new ProfileMsg(commandId))
            .SubmitAsync<string>()).Body;
    }
}
