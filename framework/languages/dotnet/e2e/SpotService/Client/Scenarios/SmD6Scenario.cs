using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;

namespace SpotService.Client.Scenarios;

internal static class SmD6Scenario
{
    public static async Task RunAsync(string sessionAStreamEndpoint, string sessionBStreamEndpoint)
    {
        await using var bound = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d6", "bound", "play-a"),
            TimeSpan.FromSeconds(10));
        await using var unbound = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionBStreamEndpoint,
            new AuthReq("actor-sm-d6-shadow", "unbound", "play-b"),
            TimeSpan.FromSeconds(10));
        var pushed = bound.WaitFor<ActorPushNotify>().Async().AsTask();
        await bound.Request(new ActorPushReq("push-bound-only"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var notify = await pushed;
        ScenarioAssert.That(notify.Payload.ActorId == "actor-sm-d6", "SM-D6 push actor mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "push-bound-only", "SM-D6 push value mismatch.");
        await Task.Delay(200);
        ScenarioAssert.That(unbound.ReceivedCount("ActorPushNotify") == 0, "SM-D6 unbound session received push.");
        Console.WriteLine("operation SpotService.sm-d6 passed");
    }
}
