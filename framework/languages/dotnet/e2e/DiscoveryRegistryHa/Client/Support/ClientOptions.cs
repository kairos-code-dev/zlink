namespace DiscoveryRegistryHa.Client.Support;

internal sealed record ClientOptions(
    string ProbeUrl,
    string ConsumerUrl,
    string Reg1ConsumerUrl,
    string Reg2ConsumerUrl,
    string Reg3ConsumerUrl,
    string Reg1Reg2ConsumerUrl,
    string ProviderAUrl,
    string ProviderBUrl,
    string DuplicateProviderUrl,
    string DuplicateProviderEndpoint,
    string EmbeddedUrl,
    string EmbeddedConsumerUrl,
    string ProviderCUrl,
    string ProviderCEndpoint,
    string Scenario,
    string LogDir,
    string Reg1Url,
    string Reg2Url,
    string Reg3Url,
    string Reg1RouterEndpoint,
    string Reg2RouterEndpoint,
    string Reg3RouterEndpoint,
    string Reg1PubEndpoint,
    string Reg2PubEndpoint,
    string Reg3PubEndpoint,
    string[] Reg2PeerPubEndpoints,
    string ApiAEndpoint,
    string ApiBEndpoint,
    string RegistryProject,
    string ProviderProject,
    string ConsumerProject,
    string EmbeddedProject)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, List<string>>(StringComparer.Ordinal);
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
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string Get(string name, string fallback = "") => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : fallback;
        string[] GetMany(string name) => values.TryGetValue(name, out var bucket) ? [.. bucket] : [];

        return new ClientOptions(
            ProbeUrl: Get("--probe-url"),
            ConsumerUrl: Get("--consumer-url"),
            Reg1ConsumerUrl: Get("--reg-1-consumer-url", Get("--consumer-url")),
            Reg2ConsumerUrl: Get("--reg-2-consumer-url", Get("--consumer-url")),
            Reg3ConsumerUrl: Get("--reg-3-consumer-url", Get("--consumer-url")),
            Reg1Reg2ConsumerUrl: Get("--reg-1-reg-2-consumer-url", Get("--consumer-url")),
            ProviderAUrl: Get("--provider-a-url"),
            ProviderBUrl: Get("--provider-b-url"),
            DuplicateProviderUrl: Get("--duplicate-provider-url"),
            DuplicateProviderEndpoint: Get("--duplicate-provider-endpoint"),
            EmbeddedUrl: Get("--embedded-url"),
            EmbeddedConsumerUrl: Get("--embedded-consumer-url"),
            ProviderCUrl: Get("--provider-c-url"),
            ProviderCEndpoint: Get("--provider-c-endpoint"),
            Scenario: Get("--scenario", "cluster"),
            LogDir: Get("--log-dir", "logs"),
            Reg1Url: Get("--reg-1-url"),
            Reg2Url: Get("--reg-2-url"),
            Reg3Url: Get("--reg-3-url"),
            Reg1RouterEndpoint: Get("--reg-1-router-endpoint"),
            Reg2RouterEndpoint: Get("--reg-2-router-endpoint"),
            Reg3RouterEndpoint: Get("--reg-3-router-endpoint"),
            Reg1PubEndpoint: Get("--reg-1-pub-endpoint"),
            Reg2PubEndpoint: Get("--reg-2-pub-endpoint"),
            Reg3PubEndpoint: Get("--reg-3-pub-endpoint"),
            Reg2PeerPubEndpoints: GetMany("--reg-2-peer-pub-endpoint"),
            ApiAEndpoint: Get("--api-a-endpoint"),
            ApiBEndpoint: Get("--api-b-endpoint"),
            RegistryProject: Get("--registry-project"),
            ProviderProject: Get("--provider-project"),
            ConsumerProject: Get("--consumer-project"),
            EmbeddedProject: Get("--embedded-project"));
    }
}
