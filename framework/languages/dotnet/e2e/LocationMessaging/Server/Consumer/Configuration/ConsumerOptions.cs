using LocationMessaging.Server.Consumer.Endpoints;
using LocationMessaging.Server.Consumer;
using Zlink.Framework.E2E.Configuration;
namespace LocationMessaging.Server.Consumer.Configuration;

internal sealed record ConsumerOptions(
    string HttpUrl,
    string LogDir,
    string TraceLabel,
    string? RedisEndpoint = null,
    string? RedisKeyPrefix = null,
    IReadOnlyList<string>? ProviderEndpoints = null)
{
    public static ConsumerOptions Parse(string[] args)
        => E2eConfiguration.Load<ConsumerOptions>(args);
}
