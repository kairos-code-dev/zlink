using DiscoveryRegistryHa.Server.Embedded;
namespace DiscoveryRegistryHa.Server.Embedded.Support;

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    uint RegistryId,
    string? RegistryPubEndpoint,
    string? RegistryRouterEndpoint,
    string? ChannelEndpoint,
    IReadOnlyList<string> PeerPubEndpoints,
    IReadOnlyList<string> DiscoveryEndpoints)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        var peers = new List<string>();
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
            if (string.Equals(key, "--peer-pub-endpoint", StringComparison.OrdinalIgnoreCase))
            {
                peers.Add(value);
            }
            else if (string.Equals(key, "--discovery-endpoint", StringComparison.OrdinalIgnoreCase))
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

        uint GetUInt(string name, uint fallback = 0) => values.TryGetValue(name, out var value)
            ? uint.Parse(value)
            : fallback;

        return new ServerOptions(
            Role: defaultRole,
            HttpUrl: Get("--http-url", "http://127.0.0.1:0"),
            LogDir: Get("--log-dir", "logs"),
            EvidenceFile: Get("--evidence-file"),
            Rid: Get("--rid", Environment.GetEnvironmentVariable("ZLINK_E2E_RID") ?? "node"),
            RegistryId: GetUInt("--registry-id", 1),
            RegistryPubEndpoint: Get("--registry-pub-endpoint"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            ChannelEndpoint: Get("--channel-endpoint"),
            PeerPubEndpoints: peers,
            DiscoveryEndpoints: discovery);
    }
}
