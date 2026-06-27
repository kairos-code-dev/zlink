using System.Collections.Concurrent;
using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD9Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint)
    {
        var observed = new ConcurrentQueue<string>();
        await using var stream = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d9", "observer", "play-a"),
            TimeSpan.FromSeconds(5),
            connector =>
            {
                connector.ObserveInbound((observation, cancellationToken) =>
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    observed.Enqueue(observation.Name);
                    return ValueTask.CompletedTask;
                });
            });
        await stream.Request(new ActorPingReq("observer-1"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        await stream.Request(new ActorPingReq("observer-2"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(observed.Count >= 2, "SM-D9 inbound observer did not observe stream replies.");
        Console.WriteLine("operation SpotService.sm-d9 passed");
    }
}
