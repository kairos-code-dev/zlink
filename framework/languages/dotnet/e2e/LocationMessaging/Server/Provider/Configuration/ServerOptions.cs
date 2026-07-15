namespace LocationMessaging.Server.Provider.Configuration;

using Zlink.Framework.E2E.Configuration;

internal sealed record ServerOptions(
    string Role,
    string HttpUrl,
    string LogDir,
    string? EvidenceFile,
    string Rid,
    string? RedisEndpoint,
    string? RedisKeyPrefix,
    string? ChannelEndpoint,
    string? ManualClientEndpoint,
    string? RouteEndpoint,
    int Weight,
    long MaxMessageSize,
    IReadOnlyList<string>? RoutePeers = null)
{
    public static ServerOptions Parse(string[] args, string defaultRole)
        => E2eConfiguration.Load<ServerOptions>(args) with { Role = defaultRole };
}
