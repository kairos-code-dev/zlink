namespace RuntimeMonitoring.Client;

internal sealed record ClientOptions(
    string TriggerUrl,
    string RegistryRouterEndpoint,
    string RegistryUrl,
    string ServiceUrl,
    string ServiceChannelEndpoint,
    string ServiceBUrl,
    string ServiceBChannelEndpoint,
    string ThrowServiceUrl,
    string ThrowChannelEndpoint,
    string FilteredServiceProject,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
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

        string Get(string name) => values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
            ? value
            : throw new ArgumentException($"{name} is required.");

        return new ClientOptions(
            TriggerUrl: Get("--trigger-url"),
            RegistryRouterEndpoint: Get("--registry-router-endpoint"),
            RegistryUrl: Get("--registry-url"),
            ServiceUrl: Get("--service-url"),
            ServiceChannelEndpoint: Get("--service-channel-endpoint"),
            ServiceBUrl: Get("--service-b-url"),
            ServiceBChannelEndpoint: Get("--service-b-channel-endpoint"),
            ThrowServiceUrl: Get("--throw-service-url"),
            ThrowChannelEndpoint: Get("--throw-channel-endpoint"),
            FilteredServiceProject: Get("--filtered-service-project"),
            LogDir: Get("--log-dir"));
    }
}
