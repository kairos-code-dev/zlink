namespace PubSub.Client;

internal sealed record ClientOptions(
    string PublisherUrl,
    string LateSubscriberUrl,
    string RegistryRouterEndpoint,
    string PublisherEndpoint,
    string PublisherProject,
    string SubscriberProject,
    string LogDir,
    string Scenario,
    string[] SubscriberUrls)
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

        string GetOne(string name) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : throw new ArgumentException($"{name} is required.");
        string[] GetMany(string name) => values.TryGetValue(name, out var bucket)
            ? [.. bucket]
            : throw new ArgumentException($"{name} is required.");
        string GetOptional(string name, string defaultValue) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : defaultValue;

        return new ClientOptions(
            PublisherUrl: GetOne("--publisher-url"),
            LateSubscriberUrl: GetOne("--late-subscriber-url"),
            RegistryRouterEndpoint: GetOne("--registry-router-endpoint"),
            PublisherEndpoint: GetOne("--publisher-endpoint"),
            PublisherProject: GetOne("--publisher-project"),
            SubscriberProject: GetOne("--subscriber-project"),
            LogDir: GetOne("--log-dir"),
            Scenario: GetOptional("--scenario", "all"),
            SubscriberUrls: GetMany("--subscriber-url"));
    }
}
