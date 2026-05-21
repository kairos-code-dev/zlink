namespace Zlink.Framework.E2ETests.MultiProcess;

[Collection(nameof(MultiProcessTestsCollection))]
public sealed class HostReadinessTests
{
    [Fact]
    public void TestHostArtifacts_ArePresent_ForCurrentTargetFramework()
    {
        var executablePath = Tests.Common.FrameworkTestEnvironment.GetTestHostExecutablePath();
        var assemblyPath = Tests.Common.FrameworkTestEnvironment.GetTestHostAssemblyPath();

        Assert.True(File.Exists(executablePath), $"Missing test host executable at '{executablePath}'.");
        Assert.True(File.Exists(assemblyPath), $"Missing test host assembly at '{assemblyPath}'.");
    }

    [Fact]
    public async Task TestHost_Reports_Ready_OnStandardOutput_And_Writes_Logs()
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(30));
        await using var process = await TestHostProcess.StartAsync(timeout.Token, "idle");

        Assert.NotNull(process.ReadyPayload);
        Assert.Equal(process.ProcessId, process.ReadyPayload!.RootElement.GetProperty("pid").GetInt32());
        Assert.Equal("idle", process.ReadyPayload.RootElement.GetProperty("mode").GetString());
        Assert.True(File.Exists(process.StandardOutputLogPath), $"Missing stdout log at '{process.StandardOutputLogPath}'.");
        Assert.True(File.Exists(process.StandardErrorLogPath), $"Missing stderr log at '{process.StandardErrorLogPath}'.");
        Assert.Contains(process.StandardOutput, static line => line.StartsWith("READY:", StringComparison.Ordinal));
    }

    [Fact]
    public async Task TestHost_Stops_When_StandardInput_ReachesEof()
    {
        var startInfo = Tests.Common.FrameworkTestEnvironment.CreateTestHostStartInfo();

        using var process = new System.Diagnostics.Process
        {
            StartInfo = startInfo,
            EnableRaisingEvents = true,
        };

        Assert.True(process.Start(), "Failed to start test host process.");

        process.StandardInput.Close();

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));

        try
        {
            await process.WaitForExitAsync(timeout.Token);
        }
        catch (OperationCanceledException)
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                await process.WaitForExitAsync();
            }

            throw;
        }

        Assert.Equal(0, process.ExitCode);
    }
}
