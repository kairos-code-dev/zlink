namespace ObservabilityOps.Server.Play.Support;

internal sealed record PlayOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string PubEndpoint,
    string LogDir,
    bool MetricsEnabled)
{
    public static PlayOptions Parse(string[] args)
    {
        var values = ParseValues(args);
        return new PlayOptions(
            Require(values, "--rid"), Require(values, "--http-url"),
            Require(values, "--redis-endpoint"), Require(values, "--redis-key-prefix"),
            Require(values, "--router-endpoint"), Require(values, "--pub-endpoint"),
            Require(values, "--log-dir"),
            !values.TryGetValue("--metrics", out var metrics)
            || !string.Equals(metrics, "off", StringComparison.OrdinalIgnoreCase));
    }

    private static Dictionary<string, string> ParseValues(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var index = 0; index < args.Length; index++)
        {
            if (!args[index].StartsWith("--", StringComparison.Ordinal) || index + 1 >= args.Length)
                throw new ArgumentException($"Invalid argument '{args[index]}'.");
            values[args[index]] = args[++index];
        }
        return values;
    }

    private static string Require(IReadOnlyDictionary<string, string> values, string key) =>
        values.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"{key} is required.");
}
