using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmD1Scenario
{
    public static async Task RunAsync(ZLinkHttpClient sessionA, string sessionAStreamEndpoint)
    {
        await WaitForControlRouteAsync(sessionA, "play-a", "sm-d1-play-a-ready");
        await using var bound = await SpotActorRequestSupport.ConnectAndAuthWithRetryAsync(
            sessionAStreamEndpoint,
            new AuthReq("actor-sm-d1", "local relay", "play-a"),
            TimeSpan.FromSeconds(10));
        var pushed = bound.WaitFor<ActorPushNotify>().Async().AsTask();
        var reply = await bound.Request(new ActorPushReq("push-local"))
            .PacketName("ActorPushReq")
            .Async<ActorPingReply>();
        var notify = await pushed;
        ScenarioAssert.That(reply.ActorId == "actor-sm-d1", "SM-D1 actor reply mismatch.");
        ScenarioAssert.That(notify.Payload.ActorId == "actor-sm-d1", "SM-D1 push actor mismatch.");
        ScenarioAssert.That(notify.Payload.Value == "push-local", "SM-D1 push value mismatch.");
        Console.WriteLine("operation SpotService.sm-d1 passed");
    }

    public static async Task WaitForControlRouteAsync(ZLinkHttpClient session, string targetRid, string marker)
    {
        var deadline = DateTimeOffset.UtcNow + TimeSpan.FromSeconds(30);
        Exception? last = null;
        while (DateTimeOffset.UtcNow < deadline)
        {
            try
            {
                var reply = (await session.Post($"/channel/control-ping/{targetRid}")
                    .Body(new ControlPingReq(marker))
                    .SubmitAsync<ControlPingReply>()).Body;
                if (reply.NodeRid == targetRid)
                {
                    return;
                }
            }
            catch (Exception ex)
            {
                last = ex;
            }

            await Task.Delay(500);
        }

        throw new InvalidOperationException(
            last is null
                ? $"Control route did not become ready: {targetRid}"
                : $"Control route did not become ready: {targetRid}. Last error: {last.Message}",
            last);
    }
}
