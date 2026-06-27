namespace RuntimeMonitoring.Server.Registry;

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
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
            {
                throw new ArgumentException($"Unexpected argument '{key}'.");
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for '{key}'.");
            }

            values[key] = args[++i];
        }

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var value)
            ? value
            : fallback;

        return new ServerOptions(
            Role: defaultRole,
            HttpUrl: Get("--http-url", "http://127.0.0.1:0"),
            LogDir: Get("--log-dir", "logs"),
            EvidenceFile: Get("--evidence-file"),
            Rid: Get("--rid", Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node"),
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ChannelEndpoint: Get("--channel-endpoint"),
            SpotRouterEndpoint: Get("--spot-router-endpoint"),
            SpotPubEndpoint: Get("--spot-pub-endpoint"));
    }
}
