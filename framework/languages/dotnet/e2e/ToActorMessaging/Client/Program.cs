using ToActorMessaging.Client.Scenarios;
using ToActorMessaging.Client.Support;

var options = ClientOptions.Parse(args);
using var context = new ToActorScenarioContext(options);

var scenarios = new Dictionary<string, Func<Task>>(StringComparer.OrdinalIgnoreCase)
{
    ["TA-A1"] = () => TaA1BoundActorMessagingScenario.RunAsync(context),
    ["TA-A2"] = () => TaA2UnboundActorMessagingScenario.RunAsync(context),
    ["TA-A3"] = () => TaA3LateBindScenario.RunAsync(context),
    ["TA-A4"] = () => TaA4DisconnectAndDestroyScenario.RunAsync(context),
    ["TA-B1"] = () => TaB1MissingActorScenario.RunAsync(context),
    ["TA-B2"] = () => TaB2StaleActorReferenceScenario.RunAsync(context),
    ["TA-B3"] = () => TaB3RouteReconnectScenario.RunAsync(context)
};

IEnumerable<string> selected = string.Equals(options.Scenario, "all", StringComparison.OrdinalIgnoreCase)
    ? scenarios.Keys
    : options.Scenario.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
foreach (var name in selected)
{
    if (!scenarios.TryGetValue(name, out var scenario))
        throw new ArgumentException($"Unknown scenario '{name}'.");
    await scenario();
}

Console.WriteLine("to-actor-messaging e2e result=passed");

internal sealed record ClientOptions(
    string ActorUrl,
    string ActorBUrl,
    string CallerUrl,
    string SessionAStreamEndpoint,
    string SessionBStreamEndpoint,
    string SessionAUrl,
    string SessionBUrl,
    string Scenario)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i += 2)
        {
            var key = args[i].TrimStart('-');
            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{args[i]}'.");
            values[key] = args[i + 1];
        }

        return new ClientOptions(
            values["actor-url"],
            values["actor-b-url"],
            values["caller-url"],
            values["session-a-stream-endpoint"],
            values["session-b-stream-endpoint"],
            values["session-a-url"],
            values["session-b-url"],
            values.TryGetValue("scenario", out var scenario) ? scenario : "all");
    }
}
