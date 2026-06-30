using RegistryMessaging.Server.Consumer.Endpoints;
using RegistryMessaging.Server.Consumer;
namespace RegistryMessaging.Server.Consumer.Configuration;

internal sealed record ConsumerOptions(
    string HttpUrl,
    string LogDir,
    string TraceLabel,
    IReadOnlyList<string> ProviderEndpoints,
    string? RegistryRouterEndpoint)
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
        var registryRouterEndpoint = GetSingleOrNull(values, "registry-router-endpoint");
        if (endpoints.Length == 0 && string.IsNullOrWhiteSpace(registryRouterEndpoint))
        {
            throw new ArgumentException("--provider-endpoint or --registry-router-endpoint is required.");
        }

        return new ConsumerOptions(
            GetSingle(values, "http-url", "http://127.0.0.1:0"),
            GetSingle(values, "log-dir", Path.Combine(Path.GetTempPath(), "zlink-dotnet-e2e-log")),
            GetSingle(values, "trace-label", "consumer"),
            endpoints,
            registryRouterEndpoint);
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
