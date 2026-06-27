namespace SpotService.Client;

internal sealed record ClientOptions(
    string GatewayUrl,
    string PlayAUrl,
    string PlayBUrl,
    string MultiAUrl,
    string MultiBUrl,
    string SessionAUrl,
    string SessionAStreamEndpoint,
    string SessionATlsStreamEndpoint,
    string SessionBStreamEndpoint,
    string OperationGroup)
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
            values.TryGetValue("gateway-url", out var gatewayUrl) && !string.IsNullOrWhiteSpace(gatewayUrl)
                ? gatewayUrl
                : throw new ArgumentException("--gateway-url is required."),
            values.TryGetValue("play-a-url", out var playAUrl) && !string.IsNullOrWhiteSpace(playAUrl)
                ? playAUrl
                : gatewayUrl,
            values.TryGetValue("play-b-url", out var playBUrl) && !string.IsNullOrWhiteSpace(playBUrl)
                ? playBUrl
                : gatewayUrl,
            values.TryGetValue("multi-a-url", out var multiAUrl) && !string.IsNullOrWhiteSpace(multiAUrl)
                ? multiAUrl
                : gatewayUrl,
            values.TryGetValue("multi-b-url", out var multiBUrl) && !string.IsNullOrWhiteSpace(multiBUrl)
                ? multiBUrl
                : gatewayUrl,
            values.TryGetValue("session-a-url", out var sessionAUrl) && !string.IsNullOrWhiteSpace(sessionAUrl)
                ? sessionAUrl
                : gatewayUrl,
            values.GetValueOrDefault("session-a-stream-endpoint", ""),
            values.GetValueOrDefault("session-a-tls-stream-endpoint", ""),
            values.GetValueOrDefault("session-b-stream-endpoint", ""),
            values.GetValueOrDefault("operation-group", "all"));
    }
}
