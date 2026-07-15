namespace ObservabilityOps.Server.Play.Support;

using Zlink.Framework.E2E.Configuration;

internal sealed record PlayOptions(
    string Rid,
    string HttpUrl,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string RouterEndpoint,
    string PubEndpoint,
    string LogDir,
    bool MetricsEnabled = true)
{
    public static PlayOptions Parse(string[] args)
        => E2eConfiguration.Load<PlayOptions>(args);
}
