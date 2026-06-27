using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

// Verifies recovery after the play-a process crashes.
internal static class SmG1Scenario
{
    public static async Task RunAsync(
        string playAUrl,
        string sessionAStreamEndpoint,
        string sessionBStreamEndpoint)
    {
        await using var playA = CreateClient(sessionAStreamEndpoint);
        await using var playB = CreateClient(sessionBStreamEndpoint);
        await playA.Connect.Async();
        await playB.Connect.Async();
        await playA.Request(new AuthReq("actor-sm-g1-crash", "crash-owner", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthReply>();
        await playB.Request(new AuthReq("actor-sm-g1-survivor", "survivor", "play-b"))
            .PacketName("AuthReq")
            .Async<AuthReply>();

        var beforeCrash = await playA.Request(new ActorPingReq("before-crash"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(beforeCrash.NodeRid == "play-a", "SM-G1 play-a actor setup mismatch.");
        var beforeSurvivor = await playB.Request(new ActorPingReq("before-crash"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(beforeSurvivor.NodeRid == "play-b", "SM-G1 play-b actor setup mismatch.");

        await CrashNodeAsync(playAUrl);
        await WaitForFailureAsync(
            playA.Request(new ActorPingReq("after-crash"))
                .PacketName("ActorPingReq")
                .Timeout(TimeSpan.FromSeconds(1))
                .Async<ActorPingReply>().AsTask(),
            "SM-G1 expected play-a actor request to fail after crash.");

        var survivor = await playB.Request(new ActorPingReq("after-crash"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(survivor.ActorId == "actor-sm-g1-survivor", "SM-G1 survivor actor mismatch.");
        ScenarioAssert.That(survivor.NodeRid == "play-b", "SM-G1 survivor node mismatch.");

        await using var recovered = CreateClient(sessionBStreamEndpoint);
        await recovered.Connect.Async();
        await recovered.Request(new AuthReq("actor-sm-g1-crash", "recovered-on-play-b", "play-b"))
            .PacketName("AuthReq")
            .Async<AuthReply>();
        var rebound = await recovered.Request(new ActorPingReq("rebound"))
            .PacketName("ActorPingReq")
            .Async<ActorPingReply>();
        ScenarioAssert.That(rebound.ActorId == "actor-sm-g1-crash", "SM-G1 rebound actor mismatch.");
        ScenarioAssert.That(rebound.NodeRid == "play-b", "SM-G1 rebound node mismatch.");

        Console.WriteLine("operation SpotService.sm-g1 passed");
    }

    static IZlinkStreamConnector CreateClient(string endpoint)
    {
        return ZlinkStreamConnectorFactory.Create(new ZlinkStreamConnectorOptions
        {
            Endpoint = new Uri(endpoint),
        });
    }

    static async Task CrashNodeAsync(string playAUrl)
    {
        using var http = ZLinkHttpClient.Create(playAUrl)
            .Json()
            .Timeout(TimeSpan.FromSeconds(5))
            .Build();
        var response = await http.Post("/crash").SubmitRawAsync();
        ScenarioAssert.That(
            response.Status >= 200 && response.Status < 300,
            $"SM-G1 crash endpoint returned HTTP {response.Status}.");
        await Task.Delay(TimeSpan.FromMilliseconds(200));
    }

    static async Task WaitForFailureAsync(Task task, string message)
    {
        try
        {
            await task;
        }
        catch
        {
            return;
        }

        throw new InvalidOperationException(message);
    }
}
