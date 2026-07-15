using System.Text.RegularExpressions;
using Xunit;

namespace Zlink.Framework.SampleRegressionTests;

public sealed partial class RegressionTests
{
    [Fact]
    public void E2E_Runners_Default_Local_Readiness_To_Three_Seconds()
    {
        var runners = Directory.EnumerateFiles(ResolveE2eRoot(), "run_e2e.sh", SearchOption.AllDirectories)
            .Order(StringComparer.Ordinal)
            .ToArray();

        Assert.Equal(11, runners.Length);
        foreach (var runner in runners)
        {
            var text = File.ReadAllText(runner);
            Assert.Matches(
                new Regex(
                    "LOCAL_READINESS_TIMEOUT_SECONDS=\\\"\\$\\{[A-Z0-9_]+:-3\\}\\\"",
                    RegexOptions.CultureInvariant),
                text);
            Assert.Contains("LOCAL_READINESS_POLL_SECONDS=0.1", text, StringComparison.Ordinal);
        }
    }

    [Fact]
    public void LocationMessaging_Role_Requests_Do_Not_Retry_Route_Convergence()
    {
        var endpoints = File.ReadAllText(Path.Combine(
            ResolveE2eRoot(),
            "LocationMessaging",
            "Server",
            "Provider",
            "Endpoints",
            "ProviderEndpoints.cs"));

        Assert.DoesNotContain("WithRetryAsync", endpoints, StringComparison.Ordinal);
        Assert.DoesNotContain("IsRetriableRequestStartupFailure", endpoints, StringComparison.Ordinal);
    }

    [Fact]
    public void LocationMessaging_RmC9_Fills_A_Bounded_High_Water_Mark()
    {
        var root = Path.Combine(ResolveE2eRoot(), "LocationMessaging");
        var scenario = File.ReadAllText(Path.Combine(
            root, "Client", "Scenarios", "RmC9BackpressureScenario.cs"));
        var consumer = File.ReadAllText(Path.Combine(root, "Server", "Consumer", "ConsumerHostFactory.cs"));
        var provider = File.ReadAllText(Path.Combine(root, "Server", "Provider", "ProviderHostFactory.cs"));

        Assert.Contains("private const int SlowSendCount = 64", scenario, StringComparison.Ordinal);
        Assert.Contains("private const int PressureEvidenceCount = 5", scenario, StringComparison.Ordinal);
        Assert.Contains("SendHighWaterMark = 4", consumer, StringComparison.Ordinal);
        Assert.Contains("ReceiveHighWaterMark = 4", provider, StringComparison.Ordinal);
        Assert.Contains("evidence.Count(line => line.Contains(marker", scenario, StringComparison.Ordinal);
        Assert.DoesNotContain("Task.Delay(TimeSpan.FromSeconds(10))", scenario, StringComparison.Ordinal);
    }

    [Fact]
    public void State_Observation_Uses_Role_Server_Bounded_Waits()
    {
        var root = ResolveE2eRoot();
        var locationScenarios = new[]
        {
            "RmB1ScaleOutScenario.cs",
            "RmB2ScaleInScenario.cs",
            "RmA4SameRidFailoverScenario.cs"
        };
        foreach (var scenarioName in locationScenarios)
        {
            var source = File.ReadAllText(Path.Combine(
                root, "LocationMessaging", "Client", "Scenarios", scenarioName));
            Assert.DoesNotContain("Get(\"/locations/peers", source, StringComparison.Ordinal);
            Assert.Contains("Post(\"/locations/peers/wait\")", source, StringComparison.Ordinal);
        }

        var storeProbe = File.ReadAllText(Path.Combine(
            root, "StoreFailure", "Client", "Support", "SfProbe.cs"));
        Assert.DoesNotContain("last = await TryGetPeersAsync", storeProbe, StringComparison.Ordinal);
        Assert.DoesNotContain("last = await GetStatusAsync", storeProbe, StringComparison.Ordinal);
        Assert.Contains("Post(\"/query/peers/wait\")", storeProbe, StringComparison.Ordinal);
        Assert.Contains("Post(\"/query/status/wait\")", storeProbe, StringComparison.Ordinal);

        var trafficProbe = File.ReadAllText(Path.Combine(
            root, "ResilienceLifecycle", "Client", "Support", "ProviderTrafficProbe.cs"));
        Assert.DoesNotContain("provider.Get(\"/evidence\")", trafficProbe, StringComparison.Ordinal);
        Assert.Contains("provider.Post(\"/evidence/wait\")", trafficProbe, StringComparison.Ordinal);
    }

    [Fact]
    public void Config_9_And_10_Keep_One_Client_Scenario_Per_File()
    {
        var root = ResolveE2eRoot();
        AssertScenarioFiles(root, "SpotActorTransfer", "St", 20);
        AssertScenarioFiles(root, "ToActorMessaging", "Ta", 7);
    }

    [Fact]
    public void SpotActorTransfer_Separates_Host_Endpoints_From_Actor_Runtime()
    {
        var actorNode = Path.Combine(
            ResolveE2eRoot(),
            "SpotActorTransfer",
            "Server",
            "ActorNode");
        var program = File.ReadAllText(Path.Combine(actorNode, "Program.cs"));
        var endpoints = File.ReadAllText(Path.Combine(actorNode, "ActorNodeEndpoints.cs"));
        var runtime = File.ReadAllText(Path.Combine(actorNode, "ActorRuntime.cs"));

        Assert.DoesNotContain("app.MapPost", program, StringComparison.Ordinal);
        Assert.Contains("ActorNodeEndpoints.Map(app, options)", program, StringComparison.Ordinal);
        Assert.Contains("app.MapPost(\"/actors\"", endpoints, StringComparison.Ordinal);
        Assert.DoesNotContain("class TransferActorAdapter", program, StringComparison.Ordinal);
        Assert.Contains("class TransferActorAdapter", runtime, StringComparison.Ordinal);
        Assert.DoesNotContain("app.MapPost", runtime, StringComparison.Ordinal);
    }

    [Fact]
    public void RuntimeMonitoring_Uses_A_Client_Trigger_And_Role_Evidence()
    {
        var root = Path.Combine(ResolveE2eRoot(), "RuntimeMonitoring");
        Assert.False(File.Exists(Path.Combine(
            root, "Server", "Trigger", "RuntimeMonitoring.Trigger.csproj")));

        var runner = File.ReadAllText(Path.Combine(root, "run_e2e.sh"));
        Assert.DoesNotContain("ZLINK_DEBUG_FRAMEWORK_TASKS", runner, StringComparison.Ordinal);
        Assert.DoesNotContain("/logs/throw-stderr", runner, StringComparison.Ordinal);

        var scenario = File.ReadAllText(Path.Combine(
            root, "Client", "Scenarios", "MonC1DispatchFailureScenario.cs"));
        Assert.DoesNotContain("throw-stderr", scenario, StringComparison.Ordinal);
        Assert.DoesNotContain("unhandled callback failed", scenario, StringComparison.Ordinal);
    }

    [Fact]
    public void ObservabilityOps_Is_A_Self_Contained_E2E_App()
    {
        var root = Path.Combine(ResolveE2eRoot(), "ObservabilityOps");
        var clientScenarios = Path.Combine(root, "Client", "Scenarios");
        Assert.True(Directory.Exists(clientScenarios));
        Assert.Equal(13, Directory.GetFiles(clientScenarios, "Obs*Scenario.cs").Length);

        foreach (var project in Directory.GetFiles(root, "*.csproj", SearchOption.AllDirectories))
        {
            var source = File.ReadAllText(project);
            Assert.DoesNotContain("samples/Bingo", source, StringComparison.OrdinalIgnoreCase);
            Assert.DoesNotContain("samples/ShoppingMall", source, StringComparison.OrdinalIgnoreCase);
        }

        var runner = File.ReadAllText(Path.Combine(root, "run_e2e.sh"));
        Assert.DoesNotContain("samples/Bingo/Client", runner, StringComparison.Ordinal);
        Assert.DoesNotContain("python3 - \"$LOG_DIR", runner, StringComparison.Ordinal);
    }

    private static void AssertScenarioFiles(string root, string config, string idPrefix, int expectedCount)
    {
        var client = Path.Combine(root, config, "Client");
        var scenarioDirectory = Path.Combine(client, "Scenarios");
        Assert.True(Directory.Exists(scenarioDirectory), $"{config} Client/Scenarios is missing.");
        var scenarios = Directory.GetFiles(scenarioDirectory, $"{idPrefix}*Scenario.cs");
        Assert.Equal(expectedCount, scenarios.Length);

        var program = File.ReadAllText(Path.Combine(client, "Program.cs"));
        Assert.DoesNotContain("async Task Run", program, StringComparison.Ordinal);
        Assert.DoesNotContain("async () =>", program, StringComparison.Ordinal);
    }
}
