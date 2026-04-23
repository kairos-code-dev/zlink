namespace Zlink.Framework.MultiProcessTests;

[Collection(nameof(MultiProcessTestsCollection))]
public sealed class TestHostReadinessTests
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
