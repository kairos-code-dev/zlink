namespace DiscoveryRegistryHa.Server.Consumer;

internal sealed record ConsumerOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    IReadOnlyList<string> DiscoveryEndpoints)
{
    public static ConsumerOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var discovery = new List<string>();
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            var value = args[++i];
            if (string.Equals(key, "--discovery-endpoint", StringComparison.OrdinalIgnoreCase))
            {
                discovery.Add(value);
            }
            else
            {
                values[key] = value;
            }
        }

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var value)
            ? value
            : fallback;

        if (discovery.Count == 0)
        {
            throw new ArgumentException("At least one --discovery-endpoint is required.");
        }

        return new ConsumerOptions(
            Get("--rid", "consumer"),
            Get("--http-url", "http://127.0.0.1:0"),
            Get("--log-dir", "logs"),
            discovery);
    }
}
