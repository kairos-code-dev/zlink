namespace Zlink.Framework.ScenarioE2E;

// The single source of truth for every scenario: its server topology (so the run
// script can launch the right processes) and the client flow that drives it.
// Adding a scenario means adding one method in a chapter file and one row here.
internal static class ScenarioCatalog
{
    private static readonly IReadOnlyList<ScenarioEntry> Entries = new[]
    {
        new ScenarioEntry("CH-001", "channel", new[] { "api-a" }, Ch02ChannelMessaging.RequestResponse),
        new ScenarioEntry("CH-002", "channel", new[] { "api-a", "api-b", "api-c" }, Ch02ChannelMessaging.ManualEndpointRoundRobin),
        new ScenarioEntry("CH-004", "route-mesh", new[] { "peer-b", "peer-c" }, Ch02ChannelMessaging.RouteMeshTargetedRequest),
        new ScenarioEntry("CH-006", "channel", new[] { "api-a" }, Ch02ChannelMessaging.SendOneWay),
        new ScenarioEntry("CH-007", "channel", new[] { "api-a" }, Ch02ChannelMessaging.RequestTimeoutLateReply),
        new ScenarioEntry("DERR-001", "channel", new[] { "api-a" }, Ch07DispatchError.UnregisteredRequest),
        new ScenarioEntry("DERR-002", "channel", new[] { "api-a" }, Ch07DispatchError.UnregisteredSend),
        new ScenarioEntry("DERR-007", "channel", new[] { "api-a" }, Ch07DispatchError.HandlerException),
    };

    public static IReadOnlyList<string> Ids => Entries.Select(entry => entry.Id).ToArray();

    public static ScenarioEntry Resolve(string id)
        => Entries.FirstOrDefault(entry => entry.Id == id)
           ?? throw new InvalidOperationException($"Unknown scenario id '{id}'.");

    // Emitted to stdout by the `topology` command so the run script knows how many
    // server processes (and which mode) the scenario needs.
    public static string DescribeTopology(string id)
    {
        var entry = Resolve(id);
        var lines = new List<string>
        {
            $"mode={entry.Mode}",
            $"servers={string.Join(',', entry.ServerNames)}",
        };
        if (entry.Mode == "route-mesh")
        {
            lines.Add("client=peer-a");
        }

        return string.Join('\n', lines);
    }
}

internal sealed record ScenarioEntry(
    string Id,
    string Mode,
    IReadOnlyList<string> ServerNames,
    Func<ScenarioContext, Task> Run);
