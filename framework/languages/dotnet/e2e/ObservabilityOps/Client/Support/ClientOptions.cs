namespace ObservabilityOps.Client.Support;

internal sealed record ClientOptions(
    string PlayAUrl, string PlayBUrl, string SessionUrl, string SessionEndpoint,
    string WorkflowAUrl, string WorkflowBUrl, string RedisEndpoint, string Scenario, string LogDir,
    string C5Phase)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 0; index < args.Length; index++)
        {
            if (!args[index].StartsWith("--", StringComparison.Ordinal) || index + 1 >= args.Length)
                throw new ArgumentException($"Invalid argument '{args[index]}'.");
            values[args[index]] = args[++index];
        }
        string Get(string key) => values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value : throw new ArgumentException($"{key} is required.");
        return new ClientOptions(Get("--play-a-url"), Get("--play-b-url"), Get("--session-url"),
            Get("--session-endpoint"), Get("--workflow-a-url"), Get("--workflow-b-url"),
            Get("--redis-endpoint"), values.GetValueOrDefault("--scenario", "all"), Get("--log-dir"),
            values.GetValueOrDefault("--c5-phase", "both"));
    }
}
