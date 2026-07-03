using LocationMessaging.Server.Consumer.Endpoints;
using LocationMessaging.Server.Consumer;
namespace LocationMessaging.Server.Consumer.Configuration;

internal sealed record ConsumerOptions(
    string HttpUrl,
    string LogDir,
    string TraceLabel,
    IReadOnlyList<string> ProviderEndpoints,
    string? RedisEndpoint,
    string? RedisKeyPrefix)
{
    public static ConsumerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            key = key[2..];
            if (!values.TryGetValue(key, out var existing))
            {
                existing = [];
                values.Add(key, existing);
            }

            existing.Add(args[++i]);
        }

        var endpoints = values.TryGetValue("provider-endpoint", out var providerEndpoints)
            ? providerEndpoints.Where(endpoint => !string.IsNullOrWhiteSpace(endpoint)).ToArray()
            : [];
        var redisEndpoint = GetSingleOrNull(values, "redis-endpoint");
        var redisKeyPrefix = GetSingleOrNull(values, "redis-key-prefix");
        if (endpoints.Length == 0 && string.IsNullOrWhiteSpace(redisEndpoint))
        {
            throw new ArgumentException("--provider-endpoint or --redis-endpoint is required.");
        }

        if (!string.IsNullOrWhiteSpace(redisEndpoint) && string.IsNullOrWhiteSpace(redisKeyPrefix))
        {
            throw new ArgumentException("--redis-key-prefix is required when --redis-endpoint is set.");
        }

        return new ConsumerOptions(
            GetSingle(values, "http-url", "http://127.0.0.1:0"),
            GetSingle(values, "log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            GetSingle(values, "trace-label", "consumer"),
            endpoints,
            redisEndpoint,
            redisKeyPrefix);
    }

    static string GetSingle(
        IReadOnlyDictionary<string, List<string>> values,
        string key,
        string defaultValue)
    {
        return values.TryGetValue(key, out var matched) && matched.Count > 0
            ? matched[^1]
            : defaultValue;
    }

    static string? GetSingleOrNull(
        IReadOnlyDictionary<string, List<string>> values,
        string key)
    {
        return values.TryGetValue(key, out var matched) && matched.Count > 0
            ? matched[^1]
            : null;
    }
}
