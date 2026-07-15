using System.Diagnostics;

namespace RuntimeMonitoring.Client.Support;

internal static class MonitoringRegistrationValidation
{
    public static Task<string> VerifyDuplicateSocketSourceAsync(ClientOptions options) =>
        RunInvalidHostAsync(options, "duplicate-socket", "Duplicate monitoring socket source");

    public static Task<string> VerifyPollingIntervalAsync(ClientOptions options) =>
        RunInvalidHostAsync(options, "zero-interval", "interval must be greater than zero");

    public static Task<string> VerifyMissingSpotSourceAsync(ClientOptions options) =>
        RunInvalidHostAsync(options, "missing-spot", "not registered");

    public static Task<string> VerifyMissingSocketSourceAsync(ClientOptions options) =>
        RunInvalidHostAsync(options, "missing-socket", "not registered");

    private static async Task<string> RunInvalidHostAsync(
        ClientOptions options,
        string validationCase,
        string expected)
    {
        var start = new ProcessStartInfo("dotnet")
        {
            UseShellExecute = false,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        start.ArgumentList.Add("run");
        start.ArgumentList.Add("--no-build");
        start.ArgumentList.Add("--project");
        start.ArgumentList.Add(options.ValidationHostProject);
        start.ArgumentList.Add("--");
        start.ArgumentList.Add("--case");
        start.ArgumentList.Add(validationCase);
        using var process = Process.Start(start)
                            ?? throw new InvalidOperationException("Failed to start monitoring validation host.");
        var stdout = process.StandardOutput.ReadToEndAsync();
        var stderr = process.StandardError.ReadToEndAsync();
        await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
        var output = $"{await stdout}\n{await stderr}";
        ScenarioAssert.That(process.ExitCode != 0,
            $"MON-B2 validation host '{validationCase}' unexpectedly started.");
        ScenarioAssert.That(output.Contains(expected, StringComparison.Ordinal),
            $"MON-B2 validation host '{validationCase}' did not report '{expected}'.");
        return $"mon-b2|case={validationCase}|exit={process.ExitCode}|error={expected}";
    }
}
