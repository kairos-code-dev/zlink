namespace RuntimeMonitoring.Server.Service.Support;

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RedisEndpoint,
    string? RedisKeyPrefix,
    string? ChannelEndpoint,
    string? SpotRouterEndpoint,
    string? SpotPubEndpoint)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Unexpected argument '{key}'.");

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{key}'.");

            values[key] = args[++i];
        }

        string Get(string name, string fallback = "")
        {
            return values.TryGetValue(name, out var value)
                ? value
                : fallback;
        }

        return new ServerOptions(
            defaultRole,
            Get("--http-url", "http://127.0.0.1:0"),
            Get("--log-dir", "logs"),
            Get("--evidence-file"),
            Get("--rid", "node"),
            Get("--redis-endpoint"),
            Get("--redis-key-prefix"),
            Get("--channel-endpoint"),
            Get("--spot-router-endpoint"),
            Get("--spot-pub-endpoint"));
    }
}
