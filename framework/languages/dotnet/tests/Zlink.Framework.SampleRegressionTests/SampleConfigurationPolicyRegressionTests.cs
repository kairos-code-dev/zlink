using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    [Theory]
    [InlineData("Bingo")]
    [InlineData("DeliveryDispatch")]
    [InlineData("GameQuest")]
    [InlineData("ShoppingMall")]
    [InlineData("SupportChat")]
    [InlineData("TicTacToe")]
    [InlineData("ZoneWorld")]
    public void CanonicalSampleApplicationCodeDoesNotReadEnvironmentVariables(string sampleName)
    {
        var sampleRoot = ResolveSampleRoot(sampleName);
        var sourceFiles = Directory.EnumerateFiles(sampleRoot, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}"))
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}"));

        foreach (var sourceFile in sourceFiles)
        {
            var source = File.ReadAllText(sourceFile);
            Assert.DoesNotContain("Environment.GetEnvironmentVariable", source, StringComparison.Ordinal);
            Assert.DoesNotContain("DirectoryFromEnvironment", source, StringComparison.Ordinal);
        }
    }

    [Theory]
    [InlineData("Bingo")]
    [InlineData("DeliveryDispatch")]
    [InlineData("GameQuest")]
    [InlineData("ShoppingMall")]
    [InlineData("SupportChat")]
    [InlineData("TicTacToe")]
    [InlineData("ZoneWorld")]
    public void CanonicalSampleServersDoNotAcceptIndividualConfigurationOptions(string sampleName)
    {
        var serverRoot = Path.Combine(ResolveSampleRoot(sampleName), "Server");
        var sourceFiles = Directory.EnumerateFiles(serverRoot, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}"));

        foreach (var sourceFile in sourceFiles)
        {
            var source = File.ReadAllText(sourceFile);
            Assert.DoesNotContain("\"--node\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("\"--instance\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("\"--role\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("\"--mode\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("\"--redis-endpoint\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("\"--redis-key-prefix\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("\"--log-dir\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("FromEnvironment", source, StringComparison.Ordinal);
        }
    }

    [Theory]
    [InlineData("Bingo")]
    [InlineData("DeliveryDispatch")]
    [InlineData("GameQuest")]
    [InlineData("ShoppingMall")]
    [InlineData("SupportChat")]
    [InlineData("TicTacToe")]
    [InlineData("ZoneWorld")]
    public void CanonicalSampleConfigurationLoadersUseIConfigurationBinding(string sampleName)
    {
        var configurationRoot = Path.Combine(ResolveSampleRoot(sampleName), "Server", "Configuration");
        var loaders = Directory.EnumerateFiles(configurationRoot, "*.cs", SearchOption.AllDirectories)
            .Select(File.ReadAllText)
            .Where(static source => source.Contains("\"--config\"", StringComparison.Ordinal))
            .ToArray();

        Assert.NotEmpty(loaders);
        foreach (var source in loaders)
        {
            Assert.Contains("ConfigurationBuilder", source, StringComparison.Ordinal);
            Assert.Contains("AddJsonFile", source, StringComparison.Ordinal);
            Assert.DoesNotContain("JsonSerializer.Deserialize", source, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void AllE2eApplicationsUseTypedFileConfigurationWithoutEnvironmentAccess()
    {
        var e2eRoot = Path.Combine(ResolveDotnetRoot(), "e2e");
        var sourceFiles = Directory.EnumerateFiles(e2eRoot, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}"))
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}bin{Path.DirectorySeparatorChar}"))
            .Where(static path => !path.EndsWith("E2eConfiguration.cs", StringComparison.Ordinal));

        foreach (var sourceFile in sourceFiles)
        {
            var source = File.ReadAllText(sourceFile);
            Assert.DoesNotContain("Environment.GetEnvironmentVariable", source, StringComparison.Ordinal);
            Assert.DoesNotContain("Environment.SetEnvironmentVariable", source, StringComparison.Ordinal);
            Assert.DoesNotContain("AddEnvironmentVariables", source, StringComparison.Ordinal);
            Assert.DoesNotContain("StartsWith(\"--\"", source, StringComparison.Ordinal);
            Assert.DoesNotContain("TrimStart('-')", source, StringComparison.Ordinal);
        }

        var runners = Directory.EnumerateFiles(e2eRoot, "run_e2e.sh", SearchOption.AllDirectories);
        foreach (var runner in runners)
        {
            var source = File.ReadAllText(runner);
            Assert.Contains("umask 077", source, StringComparison.Ordinal);
            Assert.Contains("CONFIG_DIR=\"$(mktemp -d)\"", source, StringComparison.Ordinal);
            Assert.Contains("rm -rf \"$CONFIG_DIR\"", source, StringComparison.Ordinal);
            Assert.Contains("write_role_config.py", source, StringComparison.Ordinal);
            Assert.DoesNotContain("env ZLINK_E2E_RID", source, StringComparison.Ordinal);
        }
    }
}
