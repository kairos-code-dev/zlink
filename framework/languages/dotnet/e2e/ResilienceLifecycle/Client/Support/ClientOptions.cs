namespace ResilienceLifecycle.Client;

internal sealed record ClientOptions(
    string ConsumerUrl,
    string RegistryUrl,
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint,
    string ProviderAUrl,
    string ProviderAEndpoint,
    string ProviderAEvidenceFile,
    string ProviderBUrl,
    string ProviderBEndpoint,
    string ProviderBEvidenceFile,
    string ProviderBRemapUrl,
    string ProviderBRemapEndpoint,
    string ProviderBGreenUrl,
    string ProviderBGreenEndpoint,
    string RegistryProject,
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
            if (!key.StartsWith("--", StringComparison.Ordinal))
            {
                continue;
            }

            if (i + 1 >= args.Length)
            {
                throw new ArgumentException($"Missing value for {key}.");
            }

            values[key[2..]] = args[++i];
        }

        return new ClientOptions(
            values.TryGetValue("consumer-url", out var consumerUrl) && !string.IsNullOrWhiteSpace(consumerUrl)
                ? consumerUrl
                : throw new ArgumentException("--consumer-url is required."),
            Require(values, "registry-url"),
            Require(values, "registry-pub-endpoint"),
            Require(values, "registry-router-endpoint"),
            values.TryGetValue("provider-a-url", out var providerAUrl) && !string.IsNullOrWhiteSpace(providerAUrl)
                ? providerAUrl
                : throw new ArgumentException("--provider-a-url is required."),
            Require(values, "provider-a-endpoint"),
            Require(values, "provider-a-evidence-file"),
            values.TryGetValue("provider-b-url", out var providerBUrl) && !string.IsNullOrWhiteSpace(providerBUrl)
                ? providerBUrl
                : throw new ArgumentException("--provider-b-url is required."),
            Require(values, "provider-b-endpoint"),
            Require(values, "provider-b-evidence-file"),
            Require(values, "provider-b-remap-url"),
            Require(values, "provider-b-remap-endpoint"),
            Require(values, "provider-b-green-url"),
            Require(values, "provider-b-green-endpoint"),
            Require(values, "registry-project"),
            Require(values, "provider-project"),
            Require(values, "log-dir"),
            values.TryGetValue("scenario", out var scenario) && !string.IsNullOrWhiteSpace(scenario)
                ? scenario
                : "all");
    }

    static string Require(IReadOnlyDictionary<string, string> values, string name) =>
        values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"--{name} is required.");
}
