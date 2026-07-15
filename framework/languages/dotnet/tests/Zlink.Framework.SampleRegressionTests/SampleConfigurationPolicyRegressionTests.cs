using Xunit;
using System.Text.RegularExpressions;

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

    [Fact]
    public void E2eOptionalConstructorSettingsHaveExplicitDefaults()
    {
        var e2eRoot = Path.Combine(ResolveDotnetRoot(), "e2e");
        var optionRecords = Directory.EnumerateFiles(e2eRoot, "*.cs", SearchOption.AllDirectories)
            .Where(static path => !path.Contains($"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}"))
            .Select(path => (Path: path, Source: File.ReadAllText(path)))
            .Where(static file => file.Source.Contains("E2eConfiguration.Load<", StringComparison.Ordinal))
            .SelectMany(static file => Regex.Matches(
                    file.Source,
                    @"(?:internal|public)\s+sealed\s+record\s+\w+Options\s*\((?<parameters>.*?)\)\s*(?:\{|;)",
                    RegexOptions.Singleline)
                .Select(match => (file.Path, Parameters: match.Groups["parameters"].Value)));

        foreach (var (path, parameters) in optionRecords)
        {
            var missingDefaults = Regex.Matches(
                    parameters,
                    @"\b[\w<>]+\?\s+\w+\s*(?=,|$)",
                    RegexOptions.Multiline)
                .Select(static match => match.Value)
                .ToArray();
            Assert.True(
                missingDefaults.Length == 0,
                $"{path} has nullable constructor settings without '= null': {string.Join(", ", missingDefaults)}");
        }
    }

    [Fact]
    public void SampleRunnersDoNotExposeEnvironmentConfigurationFallbacks()
    {
        var forbidden = new[]
        {
            "SAMPLE_RUN_DIR",
            "KEEP_RUN_DIR",
            "BASE_PORT",
            "ZONEWORLD_BROWSER_SMOKE",
            "BINGO_API_A_CHANNEL_ENDPOINT:-"
        };

        foreach (var runner in Directory.EnumerateFiles(
                     Path.Combine(ResolveDotnetRoot(), "samples"),
                     "run_sample.sh",
                     SearchOption.AllDirectories))
        {
            var source = File.ReadAllText(runner);
            foreach (var marker in forbidden)
                Assert.DoesNotContain(marker, source, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void ZoneWorldBrowserLoadsRunnerProvidedStaticConfiguration()
    {
        var browserRoot = Path.GetFullPath(Path.Combine(
            ResolveDotnetRoot(), "..", "shared_sample", "zoneworld", "client"));
        var runtime = File.ReadAllText(Path.Combine(browserRoot, "src", "shared", "config", "runtime.ts"));
        var liveTest = File.ReadAllText(Path.Combine(browserRoot, "tests", "live", "server.spec.ts"));
        var runner = File.ReadAllText(Path.Combine(ResolveSampleRoot("ZoneWorld"), "run_sample.sh"));

        Assert.Contains("fetch('/config.json'", runtime, StringComparison.Ordinal);
        Assert.DoesNotContain("import.meta.env", runtime, StringComparison.Ordinal);
        Assert.DoesNotContain("location.search", runtime, StringComparison.Ordinal);
        Assert.DoesNotContain("process.env", liveTest, StringComparison.Ordinal);
        Assert.DoesNotContain("ZONEWORLD_", runner, StringComparison.Ordinal);
        Assert.Contains("browser_dist/config.json", runner, StringComparison.Ordinal);
    }

    [Fact]
    public void SampleAndE2eClientsUseTheConnectorAssertionSurface()
    {
        var roots = new[]
        {
            Path.Combine(ResolveDotnetRoot(), "samples"),
            Path.Combine(ResolveDotnetRoot(), "e2e")
        };
        foreach (var root in roots)
        foreach (var sourceFile in Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
                     .Where(static path => path.Contains(
                         $"{Path.DirectorySeparatorChar}Client{Path.DirectorySeparatorChar}"))
                     .Where(static path => !path.Contains(
                         $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")))
        {
            var source = File.ReadAllText(sourceFile);
            Assert.DoesNotContain("class ScenarioAssert", source, StringComparison.Ordinal);
            Assert.DoesNotContain("static class ScenarioAssert", source, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void SampleAndE2eClientsDoNotSynchronouslyUnwrapAsyncOperations()
    {
        var roots = new[]
        {
            Path.Combine(ResolveDotnetRoot(), "samples"),
            Path.Combine(ResolveDotnetRoot(), "e2e")
        };
        foreach (var root in roots)
        foreach (var sourceFile in Directory.EnumerateFiles(root, "*.cs", SearchOption.AllDirectories)
                     .Where(static path => path.Contains(
                         $"{Path.DirectorySeparatorChar}Client{Path.DirectorySeparatorChar}"))
                     .Where(static path => !path.Contains(
                         $"{Path.DirectorySeparatorChar}obj{Path.DirectorySeparatorChar}")))
        {
            var source = File.ReadAllText(sourceFile);
            Assert.DoesNotContain(".AsTask().GetAwaiter().GetResult()", source, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void ZoneWorldBotTimerAppliesBackpressureToActorMovement()
    {
        var zoneWorld = ResolveSampleRoot("ZoneWorld");
        var spot = File.ReadAllText(Path.Combine(
            zoneWorld,
            "Server", "ZoneNode", "Infrastructure", "ZLink", "Spots", "ZoneSpot.cs"));
        var handlers = File.ReadAllText(Path.Combine(
            zoneWorld,
            "Server", "ZoneNode", "Infrastructure", "ZLink", "Spots", "Handlers",
            "PlayerMoveHandlers.cs"));

        Assert.Contains("RequestToActor(actorRef.Value, new BotTickReq())", spot, StringComparison.Ordinal);
        Assert.Contains(".Yield<BotTickRes>(cancellationToken)", spot, StringComparison.Ordinal);
        Assert.DoesNotContain("SendToActor(actorRef.Value, new BotTick", spot, StringComparison.Ordinal);
        Assert.Contains("IZLinkSpotActorRequestHandler<ZoneSpot, PlayerActor, BotTickReq, BotTickRes>",
            handlers, StringComparison.Ordinal);
    }

    [Fact]
    public void EveryE2eScenarioStartsWithItsVerificationPurpose()
    {
        var e2eRoot = Path.Combine(ResolveDotnetRoot(), "e2e");
        var scenarioFiles = Directory.EnumerateFiles(
                e2eRoot,
                "*Scenario.cs",
                SearchOption.AllDirectories)
            .Where(static path => path.Contains(
                $"{Path.DirectorySeparatorChar}Client{Path.DirectorySeparatorChar}Scenarios{Path.DirectorySeparatorChar}"))
            .ToArray();

        Assert.NotEmpty(scenarioFiles);
        foreach (var scenarioFile in scenarioFiles)
        {
            var firstLine = File.ReadLines(scenarioFile).FirstOrDefault();
            Assert.True(
                firstLine?.StartsWith("// Verifies ", StringComparison.Ordinal) == true,
                $"{scenarioFile} must start with a short verification-purpose comment.");
        }
    }
}
