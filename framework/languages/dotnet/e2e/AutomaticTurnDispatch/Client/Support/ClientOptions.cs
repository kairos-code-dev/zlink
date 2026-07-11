namespace AutomaticTurnDispatch.Client.Support;

internal sealed record ClientOptions(
    string SessionAStreamEndpoint,
    string SessionBStreamEndpoint,
    string Scenario,
    string RequestId,
    string SpotRid)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            if (!args[i].StartsWith("--", StringComparison.Ordinal)) continue;

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for {args[i]}.");

            values[args[i][2..]] = args[++i];
        }

        return new ClientOptions(
            Required(values, "session-a-stream-endpoint"),
            Required(values, "session-b-stream-endpoint"),
            values.TryGetValue("scenario", out var scenario) && !string.IsNullOrWhiteSpace(scenario)
                ? scenario
                : "full",
            values.TryGetValue("request-id", out var requestId) && !string.IsNullOrWhiteSpace(requestId)
                ? requestId
                : $"client-{Guid.NewGuid():N}",
            values.TryGetValue("spot-rid", out var spotRid) && !string.IsNullOrWhiteSpace(spotRid)
                ? spotRid
                : $"await-client-{Guid.NewGuid():N}");
    }

    private static string Required(Dictionary<string, string> values, string key)
    {
        return values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{key} is required.");
    }
}