namespace DiscoveryRegistryHa.Server.Probe;

internal sealed record ProbeOptions(
    string HttpUrl,
    string RegistryRouterEndpoint,
    string LogDir)
{
    public static ProbeOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
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

            values[key] = args[++i];
        }

        string Require(string name) => values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"{name} is required.");

        return new ProbeOptions(
            Require("--http-url"),
            Require("--registry-router-endpoint"),
            values.TryGetValue("--log-dir", out var logDir) ? logDir : "logs");
    }
}
