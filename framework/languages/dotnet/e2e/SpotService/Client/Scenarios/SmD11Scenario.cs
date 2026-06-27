using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD11Scenario
{
    public static async Task RunAsync(ZLinkHttpClient sessionA, string sessionAStreamEndpoint)
    {
        await using var stream = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d11", "stream and channel", "play-a"),
            TimeSpan.FromSeconds(5));
        var streamReply = await stream.Request(new ActorPingReq("stream-side"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(streamReply.ActorId == "actor-sm-d11", "SM-D11 stream request actor mismatch.");
        var channelReply = (await sessionA.Post("/channel/control-ping/play-a")
            .Body(new ControlPingReq("channel-side"))
            .SubmitAsync<ControlPingReply>()).Body;
        ScenarioAssert.That(channelReply.NodeRid == "play-a", "SM-D11 channel request node mismatch.");
        ScenarioAssert.That(channelReply.Value == "channel-side", "SM-D11 channel reply value mismatch.");
        Console.WriteLine("operation SpotService.sm-d11 passed");
    }
}
