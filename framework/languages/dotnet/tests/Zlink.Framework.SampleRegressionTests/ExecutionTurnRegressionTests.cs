using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    private static readonly string[] ExecutionTurnScenarioIds =
    [
        "TD-A1", "TD-A2", "TD-A3", "TD-A4", "TD-A5",
        "TD-B1", "TD-B2", "TD-B3", "TD-B4",
        "TD-C1", "TD-C2", "TD-C3", "TD-C4", "TD-C5",
        "TD-D1", "TD-D2", "TD-D3",
        "TD-E1", "TD-E2", "TD-E3",
        "TD-F1", "TD-F2", "TD-F3", "TD-F4", "TD-F5", "TD-F6",
        "TD-G1"
    ];

    [Fact]
    public void ExecutionTurn_Uses_The_Canonical_TwentySeven_Scenario_Inventory()
    {
        var root = Path.Combine(ResolveE2eRoot(), "AutomaticTurnDispatch");
        var featureMap = File.ReadAllText(Path.Combine(root, "feature-map.ko.md"));
        var scenarioFiles = Directory
            .EnumerateFiles(Path.Combine(root, "Client", "Scenarios"), "Td*Scenario.cs")
            .Select(Path.GetFileNameWithoutExtension)
            .ToArray();

        Assert.Contains("config-8-execution-turn.ko.md", featureMap, StringComparison.Ordinal);
        Assert.DoesNotContain("ATD-", featureMap, StringComparison.Ordinal);
        Assert.Equal(ExecutionTurnScenarioIds.Length, scenarioFiles.Length);
        foreach (var scenarioId in ExecutionTurnScenarioIds)
        {
            Assert.Contains($"| {scenarioId} |", featureMap, StringComparison.Ordinal);
            Assert.Contains(
                scenarioFiles,
                file => file!.StartsWith(
                    scenarioId.Replace("-", string.Empty),
                    StringComparison.OrdinalIgnoreCase));
        }
    }

    [Theory]
    [InlineData("AutomaticTurnDispatch")]
    [InlineData("PubSub")]
    [InlineData("RegistrationCodec")]
    [InlineData("SpotService")]
    public void Scenario_Files_Use_Canonical_Ids_And_Descriptive_Names(string fixture)
    {
        var root = Path.Combine(ResolveE2eRoot(), fixture);
        var scenarioIds = File.ReadLines(Path.Combine(root, "feature-map.ko.md"))
            .Where(static line => line.StartsWith("| ", StringComparison.Ordinal))
            .Select(static line => line.Split('|', StringSplitOptions.TrimEntries)[1])
            .Where(static value => value.Length >= 5 && value.Contains('-', StringComparison.Ordinal))
            .ToArray();
        var scenarioFiles = Directory
            .EnumerateFiles(Path.Combine(root, "Client", "Scenarios"), "*Scenario.cs")
            .Select(Path.GetFileNameWithoutExtension)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(scenarioIds.Length, scenarioFiles.Length);
        foreach (var scenarioId in scenarioIds)
        {
            var prefix = scenarioId.Replace("-", string.Empty, StringComparison.Ordinal);
            var matches = scenarioFiles.Where(file =>
                    file!.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)
                    && file.Length > prefix.Length + "Scenario".Length
                    && char.IsUpper(file[prefix.Length]))
                .ToArray();
            Assert.Single(matches);
        }
    }
}
