namespace PubSub.Server.Publisher.Configuration;

using Zlink.Framework.E2E.Configuration;

internal sealed record PublisherOptions(
    string Rid,
    string HttpUrl,
    string LogDir,
    string RedisEndpoint,
    string RedisKeyPrefix,
    string PublisherEndpoint,
    string? EvidenceFile)
{
    public static PublisherOptions Parse(string[] args)
        => E2eConfiguration.Load<PublisherOptions>(args);
}
