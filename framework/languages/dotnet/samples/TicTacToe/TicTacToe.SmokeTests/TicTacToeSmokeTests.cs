using TicTacToe.Direct;
using TicTacToe.SessionGateway;
using Xunit;

namespace TicTacToe.SmokeTests;

public sealed class TicTacToeSmokeTests
{
    [Fact]
    public async Task DirectSampleCompletesOneMatch()
    {
        var result = await new DirectSampleScenario().RunAsync(CancellationToken.None);

        result.AssertPassed();
        Assert.Contains(result.LogLines, static line => line.Contains("client -> api: CreateMatchReq", StringComparison.Ordinal));
        Assert.Contains(result.LogLines, static line => line.Contains("play actor -> client:", StringComparison.Ordinal));
    }

    [Fact]
    public async Task SessionGatewaySampleRelaysAndReconnects()
    {
        var result = await new SessionGatewaySampleScenario().RunAsync(CancellationToken.None);

        result.AssertPassed();
        Assert.Contains(result.LogLines, static line => line.Contains("routed: request seq=", StringComparison.Ordinal));
        Assert.Contains(result.LogLines, static line => line.Contains("location: actor-session actor=player-a node=session-2", StringComparison.Ordinal));
        Assert.Contains(result.LogLines, static line => line.Contains("session-2 -> client:", StringComparison.Ordinal));
    }
}
