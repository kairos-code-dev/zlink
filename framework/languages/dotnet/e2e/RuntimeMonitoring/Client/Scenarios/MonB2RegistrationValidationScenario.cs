using RuntimeMonitoring.Client.Support;
using Zlink.HttpClient;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB2RegistrationValidationScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        using var trigger = ZLinkHttpClient.Create(options.TriggerUrl).Build();
        var scenarioEvidence = new[]
        {
            (await trigger.Post("/validation/registration/duplicate-source").SubmitAsync<string>()).Body,
            (await trigger.Post("/validation/registration/interval").SubmitAsync<string>()).Body,
            (await trigger.Post("/validation/registration/missing-spot").SubmitAsync<string>()).Body,
            (await trigger.Post("/validation/registration/missing-socket").SubmitAsync<string>()).Body
        };

        ScenarioAssert.That(
            scenarioEvidence.Any(line => line.Contains("mon-b2|duplicate=", StringComparison.Ordinal)
                                         && line.Contains("Duplicate monitoring socket source",
                                             StringComparison.Ordinal)),
            "MON-B2 duplicate source validation evidence missing.");
        ScenarioAssert.That(
            scenarioEvidence.Any(line => line.Contains("mon-b2|interval=", StringComparison.Ordinal)
                                         && line.Contains("interval must be greater than zero",
                                             StringComparison.Ordinal)),
            "MON-B2 interval validation evidence missing.");
        ScenarioAssert.That(
            scenarioEvidence.Any(line => line.Contains("mon-b2|missing-spot=not registered", StringComparison.Ordinal)),
            "MON-B2 missing spot validation evidence missing.");
        ScenarioAssert.That(
            scenarioEvidence.Any(line =>
                line.Contains("mon-b2|missing-socket=not registered", StringComparison.Ordinal)),
            "MON-B2 missing socket validation evidence missing.");
        Console.WriteLine("scenario MON-B2 passed");
    }
}