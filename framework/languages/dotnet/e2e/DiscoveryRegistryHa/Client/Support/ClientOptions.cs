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
                throw new ArgumentException($"Unexpected argument '{key}'.");

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{key}'.");

            var value = args[++i];
            if (!values.TryGetValue(key, out var bucket))
            {
                bucket = [];
                values.Add(key, bucket);
            }

            bucket.Add(value);
        }

        string Get(string name, string fallback = "")
        {
            return values.TryGetValue(name, out var bucket)
                ? bucket[^1]
                : fallback;
        }

        string[] GetMany(string name)
        {
            return values.TryGetValue(name, out var bucket) ? [.. bucket] : [];
        }

        return new ClientOptions(
            Get("--probe-url"),
            Get("--consumer-url"),
            Get("--reg-1-consumer-url", Get("--consumer-url")),
            Get("--reg-2-consumer-url", Get("--consumer-url")),
            Get("--reg-3-consumer-url", Get("--consumer-url")),
            Get("--reg-1-reg-2-consumer-url", Get("--consumer-url")),
            Get("--provider-a-url"),
            Get("--provider-b-url"),
            Get("--duplicate-provider-url"),
            Get("--duplicate-provider-endpoint"),
            Get("--embedded-url"),
            Get("--embedded-consumer-url"),
            Get("--provider-c-url"),
            Get("--provider-c-endpoint"),
            Get("--scenario", "cluster"),
            Get("--log-dir", "logs"),
            Get("--reg-1-url"),
            Get("--reg-2-url"),
            Get("--reg-3-url"),
            Get("--reg-1-router-endpoint"),
            Get("--reg-2-router-endpoint"),
            Get("--reg-3-router-endpoint"),
            Get("--reg-1-pub-endpoint"),
            Get("--reg-2-pub-endpoint"),
            Get("--reg-3-pub-endpoint"),
            GetMany("--reg-2-peer-pub-endpoint"),
            Get("--api-a-endpoint"),
            Get("--api-b-endpoint"),
            Get("--registry-project"),
            Get("--provider-project"),
            Get("--consumer-project"),
            Get("--embedded-project"));
    }
}