using System.Diagnostics;

namespace RegistrationCodec.Client.Scenarios;

// RC-A6: verifies duplicate registration fails during server startup.
internal static class InvalidRegistrationScenario
{
    public static async Task RunAsync(ClientOptions options)
    {
        var invalidHttpPort = ProcessSupport.PickPort();
        var invalidChannelPort = ProcessSupport.PickPort();
        var stdout = Path.Combine(options.LogDir, "invalid-duplicate.stdout.log");
        var stderr = Path.Combine(options.LogDir, "invalid-duplicate.stderr.log");
        using var process = new Process();
        process.StartInfo = new ProcessStartInfo("dotnet")
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        process.StartInfo.ArgumentList.Add("run");
        process.StartInfo.ArgumentList.Add("--project");
        process.StartInfo.ArgumentList.Add(options.ServerProject);
        process.StartInfo.ArgumentList.Add("--");
        process.StartInfo.ArgumentList.Add("--rid");
        process.StartInfo.ArgumentList.Add("invalid-duplicate");
        process.StartInfo.ArgumentList.Add("--http-url");
        process.StartInfo.ArgumentList.Add($"http://127.0.0.1:{invalidHttpPort}");
        process.StartInfo.ArgumentList.Add("--channel-endpoint");
        process.StartInfo.ArgumentList.Add($"tcp://127.0.0.1:{invalidChannelPort}");
        process.StartInfo.ArgumentList.Add("--invalid-mode");
        process.StartInfo.ArgumentList.Add("duplicate");
        process.StartInfo.ArgumentList.Add("--log-dir");
        process.StartInfo.ArgumentList.Add(options.LogDir);
        process.Start();

        var completed = await Task.Run(() => process.WaitForExit(15_000));
        File.WriteAllText(stdout, await process.StandardOutput.ReadToEndAsync());
        var errorText = await process.StandardError.ReadToEndAsync();
        File.WriteAllText(stderr, errorText);

        // A duplicate handler should stop the server before it becomes usable.
        ScenarioAssert.That(completed, "RC-A6 invalid registration server did not exit.");
        ScenarioAssert.That(process.ExitCode != 0, "RC-A6 invalid registration server unexpectedly started.");
        ScenarioAssert.That(
            errorText.Contains("duplicate", StringComparison.OrdinalIgnoreCase)
            || errorText.Contains("EchoManual", StringComparison.Ordinal),
            "RC-A6 invalid registration error did not mention duplicate registration.");

        Console.WriteLine("scenario RC-A6 passed");
    }
}
