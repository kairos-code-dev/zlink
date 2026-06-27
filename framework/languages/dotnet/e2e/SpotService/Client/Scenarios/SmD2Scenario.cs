using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD2Scenario
{
    public static async Task RunAsync(ZLinkHttpClient sessionA, string sessionAStreamEndpoint)
    {
        await SmD1Scenario.WaitForControlRouteAsync(sessionA, "play-b", "sm-d2-play-b-ready");
        await using var remote = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d2", "remote relay", "play-b"),
            TimeSpan.FromSeconds(10));
        var remotePushed = remote.WaitFor<ActorPushNotify>().Async().AsTask();
        var remoteReply = await remote.Request(new ActorPushReq("push-remote"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var remoteNotify = await remotePushed;
        ScenarioAssert.That(remoteReply.ActorId == "actor-sm-d2", "SM-D2 actor reply mismatch.");
        ScenarioAssert.That(remoteReply.NodeRid == "play-b", "SM-D2 remote node mismatch.");
        ScenarioAssert.That(remoteNotify.Payload.ActorId == "actor-sm-d2", "SM-D2 push actor mismatch.");
        ScenarioAssert.That(remoteNotify.Payload.Value == "push-remote", "SM-D2 push value mismatch.");
        Console.WriteLine("operation SpotService.sm-d2 passed");
    }
}
