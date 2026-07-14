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
        Assert.DoesNotContain("Task.Delay", endpoints, StringComparison.Ordinal);
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
}
