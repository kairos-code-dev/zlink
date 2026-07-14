using RuntimeMonitoring.Client.Support;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB2RegistrationValidationScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        _ = options;
        var scenarioEvidence = new[]
        {
            MonitoringRegistrationValidation.VerifyDuplicateSocketSource(),
            MonitoringRegistrationValidation.VerifyPollingInterval(),
            await MonitoringRegistrationValidation.VerifyMissingSpotSourceAsync(),
            await MonitoringRegistrationValidation.VerifyMissingSocketSourceAsync()
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
