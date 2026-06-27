using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD13Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        await using var stream = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d13", "heartbeat", "play-a"),
            TimeSpan.FromSeconds(5),
            heartbeat: new ZlinkStreamHeartbeatOptions
            {
                Enabled = true,
                Interval = TimeSpan.FromMilliseconds(200),
                Timeout = TimeSpan.FromSeconds(2),
            });
        await Task.Delay(600);
        ScenarioAssert.That(stream.IsConnected, "SM-D13 heartbeat-enabled stream disconnected.");
        Console.WriteLine("operation SpotService.sm-d13 passed");
    }
}
