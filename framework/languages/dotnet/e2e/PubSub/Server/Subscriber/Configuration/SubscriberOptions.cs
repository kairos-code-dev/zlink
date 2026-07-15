namespace PubSub.Server.Subscriber.Configuration;

using Zlink.Framework.E2E.Configuration;

internal sealed record SubscriberOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string? EvidenceFile,
    int HandlerDelayMs)
{
    public static SubscriberOptions Parse(string[] args)
        => E2eConfiguration.Load<SubscriberOptions>(args);
}
