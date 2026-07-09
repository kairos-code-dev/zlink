namespace RegistrationCodec.Client.Support;

internal sealed record ClientOptions(
    string ChannelEndpoint,
    string ServerUrl,
    string CodecRequesterUrl,
    string InvalidServerProject,
    string Scenario,
    string LogDir)
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

        string Get(string name)
        {
            return values.TryGetValue(name, out var bucket)
                ? bucket[^1]
                : throw new ArgumentException($"{name} is required.");
        }

        return new ClientOptions(
            Get("--channel-endpoint"),
            Get("--server-url"),
            Get("--codec-requester-url"),
            Get("--invalid-server-project"),
            values.TryGetValue("--scenario", out var scenario) ? scenario[^1] : "all",
            Get("--log-dir"));
    }
}
