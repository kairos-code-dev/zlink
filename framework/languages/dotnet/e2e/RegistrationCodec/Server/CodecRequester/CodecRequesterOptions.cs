namespace RegistrationCodec.CodecRequester;

internal sealed record CodecRequesterOptions(
    string Rid,
    string HttpUrl,
    string ChannelEndpoint,
    string LogDir)
{
    public static CodecRequesterOptions Parse(string[] args)
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

        return new CodecRequesterOptions(
            Rid: values.TryGetValue("--rid", out var rid) ? rid : "codec-requester",
            HttpUrl: Get("--http-url"),
            ChannelEndpoint: Get("--channel-endpoint"),
            LogDir: Get("--log-dir"));
    }
}
