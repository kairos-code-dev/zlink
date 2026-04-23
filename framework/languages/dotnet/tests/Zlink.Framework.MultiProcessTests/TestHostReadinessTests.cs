namespace Zlink.Framework.MultiProcessTests;

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
}
