namespace RegistrationCodec.Client;

internal sealed record ClientOptions(
    string ChannelEndpoint,
    string ServerUrl,
    string ServerProject,
    string LogDir)
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

        string Get(string name) => values.TryGetValue(name, out var bucket)
            ? bucket[^1]
            : throw new ArgumentException($"{name} is required.");
        return new ClientOptions(
            ChannelEndpoint: Get("--channel-endpoint"),
            ServerUrl: Get("--server-url"),
            ServerProject: Get("--server-project"),
            LogDir: Get("--log-dir"));
    }
}
