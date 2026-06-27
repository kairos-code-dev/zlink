using SpotService.Client;
using SpotService.Shared;
using Systems.Zlink.Stream.Connector.Contracts;
using Zlink.HttpClient;

namespace SpotService.Client.Scenarios;

internal static class SmB3Scenario
{
    public static async Task RunAsync(ZLinkHttpClient playA, string sessionAStreamEndpoint)
    {
        var actorId = $"actor-sm-b3-complex-{Guid.NewGuid():N}";
        await using var client = SpotActorRequestSupport.CreateClient(sessionAStreamEndpoint);
        await client.Connect.Async();
        await client.Request(new AuthReq(actorId, "complex actor", "play-a"))
            .PacketName("AuthReq")
            .Async<AuthReply>();
        var complex = await client.Request(new ComplexActorReq(
                "Ada Lovelace",
                42,
                ["alpha", "beta", "gamma"],
                new Dictionary<string, string>
                {
                    ["role"] = "analyst",
                    ["region"] = "west"
                }))
            .PacketName("ComplexActorReq")
            .Async<ComplexActorReply>();
        ScenarioAssert.That(complex.ActorId == actorId, "SM-B3 actor id mismatch.");
        ScenarioAssert.That(
            complex.DisplayName == "Ada Lovelace" && complex.Level == 42,
            "SM-B3 scalar payload mismatch.");
        ScenarioAssert.That(complex.Tags.SequenceEqual(["alpha", "beta", "gamma"]), "SM-B3 tag payload mismatch.");
        ScenarioAssert.That(
            complex.Attributes["role"] == "analyst" && complex.Attributes["region"] == "west",
            "SM-B3 attribute payload mismatch.");
        await EvidenceWait.ForAllAsync(
            playA,
            [$"actor-complex|rid=play-a|actor={actorId}"],
            "SM-B3 evidence mismatch.");
        Console.WriteLine("operation SpotService.sm-b3 passed");
    }
}
