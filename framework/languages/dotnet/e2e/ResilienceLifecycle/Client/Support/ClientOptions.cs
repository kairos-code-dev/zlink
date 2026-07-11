namespace ResilienceLifecycle.Client.Support;

internal sealed record ClientOptions(
    string ConsumerUrl,
    string TopologyUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RedisContainer,
    string ProviderAUrl,
    string ProviderAEndpoint,
    string ProviderAEvidenceFile,
    string ProviderBUrl,
    int ProviderBProcessId,
    string ProviderBEndpoint,
    string ProviderBEvidenceFile,
    string ProviderBRemapUrl,
    string ProviderBRemapEndpoint,
    string ProviderBGreenUrl,
    string ProviderBGreenEndpoint,
    string ProviderProject,
    string LogDir,
    string Scenario)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal)) continue;

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for {key}.");

            values[key[2..]] = args[++i];
        }

        return new ClientOptions(
            values.TryGetValue("consumer-url", out var consumerUrl) && !string.IsNullOrWhiteSpace(consumerUrl)
                ? consumerUrl
                : throw new ArgumentException("--consumer-url is required."),
            Require(values, "topology-url"),
            Require(values, "redis-endpoint"),
            Require(values, "redis-key-prefix"),
            Require(values, "redis-container"),
            values.TryGetValue("provider-a-url", out var providerAUrl) && !string.IsNullOrWhiteSpace(providerAUrl)
                ? providerAUrl
                : throw new ArgumentException("--provider-a-url is required."),
            Require(values, "provider-a-endpoint"),
            Require(values, "provider-a-evidence-file"),
            values.TryGetValue("provider-b-url", out var providerBUrl) && !string.IsNullOrWhiteSpace(providerBUrl)
                ? providerBUrl
                : throw new ArgumentException("--provider-b-url is required."),
            int.TryParse(Require(values, "provider-b-process-id"), out var providerBProcessId)
                ? providerBProcessId
                : throw new ArgumentException("--provider-b-process-id must be an integer."),
            Require(values, "provider-b-endpoint"),
            Require(values, "provider-b-evidence-file"),
            Require(values, "provider-b-remap-url"),
            Require(values, "provider-b-remap-endpoint"),
            Require(values, "provider-b-green-url"),
            Require(values, "provider-b-green-endpoint"),
            Require(values, "provider-project"),
            Require(values, "log-dir"),
            values.TryGetValue("scenario", out var scenario) && !string.IsNullOrWhiteSpace(scenario)
                ? scenario
                : "all");
    }

    private static string Require(IReadOnlyDictionary<string, string> values, string name)
    {
        return values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{name} is required.");
    }
}
