namespace RegistrationCodec.Server.JsonOnlyPeer;

public sealed record ServerOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string? ChannelEndpoint,
    string? EvidenceFile,
    string? InvalidMode,
    string CodecMode,
    string? JsonOnlyPeerProject)
{
    public static ServerOptions Parse(string[] args)
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

        string? Get(string name)
        {
            return values.TryGetValue(name, out var bucket) ? bucket[^1] : null;
        }

        return new ServerOptions(
            Get("--rid") ?? "reg-codec-node",
            Get("--http-url") ?? "http://127.0.0.1:0",
            Get("--log-dir") ?? "logs",
            Get("--channel-endpoint"),
            Get("--evidence-file"),
            Get("--invalid-mode"),
            Get("--codec-mode") ?? "all",
            Get("--json-only-peer-project"));
    }
}