namespace ObservabilityOps.Server.Workflow.Support;

internal sealed record WorkflowOptions(string Rid, string HttpUrl, string RedisEndpoint,
    string RedisKeyPrefix, string RouterEndpoint, string PubEndpoint, string LogDir)
{
    public static WorkflowOptions Parse(string[] args)
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
        return new WorkflowOptions(Get("--rid"), Get("--http-url"), Get("--redis-endpoint"),
            Get("--redis-key-prefix"), Get("--router-endpoint"), Get("--pub-endpoint"), Get("--log-dir"));
    }
}
