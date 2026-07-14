namespace RuntimeMonitoring.Client.Support;

internal sealed record ClientOptions(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string ServiceUrl,
    string ServiceChannelEndpoint,
    string ServiceBUrl,
    int ServiceBProcessId,
    string ServiceBChannelEndpoint,
    string ThrowServiceUrl,
    string ThrowChannelEndpoint,
    string FilteredServiceProject,
    string Scenario,
    string LogDir)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        for (var i = 0; i < args.Length; i++)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Unexpected argument '{key}'.");

            if (i + 1 >= args.Length) throw new ArgumentException($"Missing value for '{key}'.");

            values[key] = args[++i];
        }

        string Get(string name)
        {
            return values.TryGetValue(name, out var value) && !string.IsNullOrWhiteSpace(value)
                ? value
                : throw new ArgumentException($"{name} is required.");
        }

        return new ClientOptions(
            Get("--redis-endpoint"),
            Get("--redis-key-prefix"),
            Get("--service-url"),
            Get("--service-channel-endpoint"),
            Get("--service-b-url"),
            int.Parse(Get("--service-b-process-id"), System.Globalization.CultureInfo.InvariantCulture),
            Get("--service-b-channel-endpoint"),
            Get("--throw-service-url"),
            Get("--throw-channel-endpoint"),
            Get("--filtered-service-project"),
            values.TryGetValue("--scenario", out var scenario) && !string.IsNullOrWhiteSpace(scenario)
                ? scenario
                : "all",
            Get("--log-dir"));
    }
}
