// Verifies MON-B2 Registration Validation behavior.
using RuntimeMonitoring.Client.Support;

namespace RuntimeMonitoring.Client.Scenarios;

internal static class MonB2RegistrationValidationScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        _ = options;
        var scenarioEvidence = new[]
        {
            await MonitoringRegistrationValidation.VerifyDuplicateSocketSourceAsync(options),
            await MonitoringRegistrationValidation.VerifyPollingIntervalAsync(options),
            await MonitoringRegistrationValidation.VerifyMissingSpotSourceAsync(options),
            await MonitoringRegistrationValidation.VerifyMissingSocketSourceAsync(options)
        };

        ZlinkStreamAssert.Ensure(
            scenarioEvidence.Any(line => line.Contains("case=duplicate-socket", StringComparison.Ordinal)
                                         && line.Contains("Duplicate monitoring socket source",
                                             StringComparison.Ordinal)),
            "MON-B2 duplicate source validation evidence missing.");
        ZlinkStreamAssert.Ensure(
            scenarioEvidence.Any(line => line.Contains("case=zero-interval", StringComparison.Ordinal)
                                         && line.Contains("interval must be greater than zero",
                                             StringComparison.Ordinal)),
            "MON-B2 interval validation evidence missing.");
        ZlinkStreamAssert.Ensure(
            scenarioEvidence.Any(line => line.Contains("case=missing-spot", StringComparison.Ordinal)
                                         && line.Contains("not registered", StringComparison.Ordinal)),
            "MON-B2 missing spot validation evidence missing.");
        ZlinkStreamAssert.Ensure(
            scenarioEvidence.Any(line =>
                line.Contains("case=missing-socket", StringComparison.Ordinal)
                && line.Contains("not registered", StringComparison.Ordinal)),
            "MON-B2 missing socket validation evidence missing.");
        Console.WriteLine("scenario MON-B2 passed");
    }
}
