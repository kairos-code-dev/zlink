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
}
