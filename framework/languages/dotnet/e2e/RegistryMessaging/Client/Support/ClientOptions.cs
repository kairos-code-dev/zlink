namespace RegistryMessaging.Client.Support;

internal sealed record ClientOptions(
    string RegistryUrl,
    string ProviderAUrl,
    string ProviderBUrl,
    string WorkflowUrl,
    string DirectConsumerUrl,
    string SingleConsumerUrl,
    string DiscoveryConsumerUrl,
    string BackpressureConsumerUrl,
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
            if (!key.StartsWith("--", StringComparison.Ordinal)) continue;

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for {key}.");

            values[key[2..]] = args[++i];
        }

        return new ClientOptions(
            values.TryGetValue("registry-url", out var registryUrl) && !string.IsNullOrWhiteSpace(registryUrl)
                ? registryUrl
                : throw new ArgumentException("--registry-url is required."),
            values.TryGetValue("provider-a-url", out var providerAUrl) && !string.IsNullOrWhiteSpace(providerAUrl)
                ? providerAUrl
                : throw new ArgumentException("--provider-a-url is required."),
            values.TryGetValue("provider-b-url", out var providerBUrl) && !string.IsNullOrWhiteSpace(providerBUrl)
                ? providerBUrl
                : throw new ArgumentException("--provider-b-url is required."),
            values.TryGetValue("workflow-url", out var workflowUrl) && !string.IsNullOrWhiteSpace(workflowUrl)
                ? workflowUrl
                : throw new ArgumentException("--workflow-url is required."),
            values.TryGetValue("direct-consumer-url", out var directConsumerUrl)
            && !string.IsNullOrWhiteSpace(directConsumerUrl)
                ? directConsumerUrl
                : throw new ArgumentException("--direct-consumer-url is required."),
            values.TryGetValue("single-consumer-url", out var singleConsumerUrl)
            && !string.IsNullOrWhiteSpace(singleConsumerUrl)
                ? singleConsumerUrl
                : throw new ArgumentException("--single-consumer-url is required."),
            values.TryGetValue("discovery-consumer-url", out var discoveryConsumerUrl)
            && !string.IsNullOrWhiteSpace(discoveryConsumerUrl)
                ? discoveryConsumerUrl
                : throw new ArgumentException("--discovery-consumer-url is required."),
            values.TryGetValue("backpressure-consumer-url", out var backpressureConsumerUrl)
            && !string.IsNullOrWhiteSpace(backpressureConsumerUrl)
                ? backpressureConsumerUrl
                : throw new ArgumentException("--backpressure-consumer-url is required."),
            values.TryGetValue("registry-project", out var registryProject)
            && !string.IsNullOrWhiteSpace(registryProject)
                ? registryProject
                : throw new ArgumentException("--registry-project is required."),
            values.TryGetValue("provider-project", out var providerProject)
            && !string.IsNullOrWhiteSpace(providerProject)
                ? providerProject
                : throw new ArgumentException("--provider-project is required."),
            values.TryGetValue("log-dir", out var logDir) && !string.IsNullOrWhiteSpace(logDir)
                ? logDir
                : throw new ArgumentException("--log-dir is required."),
            values.TryGetValue("scenario", out var scenario) && !string.IsNullOrWhiteSpace(scenario)
                ? scenario
                : "all");
    }
}