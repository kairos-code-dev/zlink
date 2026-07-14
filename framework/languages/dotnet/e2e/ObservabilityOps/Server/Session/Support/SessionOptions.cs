namespace ObservabilityOps.Server.Session.Support;

internal sealed record SessionOptions(
    string Rid, string HttpUrl, string RedisEndpoint, string RedisKeyPrefix,
    string RouterEndpoint, string PubEndpoint, string StreamEndpoint,
    string PreferredPlayRid, string LogDir)
{
    public static SessionOptions Parse(string[] args)
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
        return new SessionOptions(Get("--rid"), Get("--http-url"), Get("--redis-endpoint"),
            Get("--redis-key-prefix"), Get("--router-endpoint"), Get("--pub-endpoint"),
            Get("--stream-endpoint"), Get("--preferred-play-rid"), Get("--log-dir"));
    }
}
