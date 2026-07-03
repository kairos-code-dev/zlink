namespace StoreFailure.Client.Support;

internal sealed record ClientOptions(
    string ConsumerUrl,
    string ConsumerProject,
    string ConsumerNwUrl,
    string ProviderProject,
    string ProviderAUrl,
    string ProviderAEndpoint,
    string ProviderAEvidenceFile,
    string ProviderBUrl,
    string ProviderBEndpoint,
    string ProviderBEvidenceFile,
    string ProviderCUrl,
    string ProviderCEndpoint,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RedisContainer,
    string LogDir,
    string Scenario,
    int LocationHeartbeatMs,
    int LocationLeaseTtlMs,
    int LocationPollingMs,
    int LocationGraceMs)
{
    public static ClientOptions Parse(string[] args)
    {
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        for (var i = 0; i + 1 < args.Length; i += 2)
        {
            var key = args[i];
            if (!key.StartsWith("--", StringComparison.Ordinal))
                throw new ArgumentException($"Unexpected argument '{key}'.");

            values[key[2..]] = args[i + 1];
        }

        string Require(string name) =>
            values.TryGetValue(name, out var value)
                ? value
                : throw new ArgumentException($"Missing required option '--{name}'.");

        return new ClientOptions(
            Require("consumer-url"),
            Require("consumer-project"),
            Require("consumer-nw-url"),
            Require("provider-project"),
            Require("provider-a-url"),
            Require("provider-a-endpoint"),
            Require("provider-a-evidence-file"),
            Require("provider-b-url"),
            Require("provider-b-endpoint"),
            Require("provider-b-evidence-file"),
            Require("provider-c-url"),
            Require("provider-c-endpoint"),
            Require("redis-endpoint"),
            Require("redis-key-prefix"),
            Require("redis-container"),
            Require("log-dir"),
            values.TryGetValue("scenario", out var scenario) ? scenario : "all",
            int.Parse(Require("location-heartbeat-ms")),
            int.Parse(Require("location-lease-ttl-ms")),
            int.Parse(Require("location-polling-ms")),
            int.Parse(Require("location-grace-ms")));
    }

    public TimeSpan HeartbeatInterval => TimeSpan.FromMilliseconds(LocationHeartbeatMs);

    public TimeSpan OwnerLeaseTtl => TimeSpan.FromMilliseconds(LocationLeaseTtlMs);

    public TimeSpan PollingInterval => TimeSpan.FromMilliseconds(LocationPollingMs);

    public TimeSpan StoreFailureGrace => TimeSpan.FromMilliseconds(LocationGraceMs);
}
