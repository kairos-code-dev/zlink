namespace PubSub.Client.Support;

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

        string GetOne(string name)
        {
            return values.TryGetValue(name, out var bucket)
                ? bucket[^1]
                : throw new ArgumentException($"{name} is required.");
        }

        string[] GetMany(string name)
        {
            return values.TryGetValue(name, out var bucket)
                ? [.. bucket]
                : throw new ArgumentException($"{name} is required.");
        }

        string GetOptional(string name, string defaultValue)
        {
            return values.TryGetValue(name, out var bucket)
                ? bucket[^1]
                : defaultValue;
        }

        return new ClientOptions(
            GetOne("--publisher-url"),
            GetOne("--late-subscriber-url"),
            GetOne("--registry-router-endpoint"),
            GetOne("--publisher-endpoint"),
            GetOne("--publisher-project"),
            GetOne("--subscriber-project"),
            GetOne("--log-dir"),
            GetOptional("--scenario", "all"),
            GetMany("--subscriber-url"));
    }
}