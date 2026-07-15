using Microsoft.Extensions.Configuration;
using Systems.Zlink;

namespace ZoneWorld.Server.Configuration;

public sealed record ZoneWorldSettings(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string LogDirectory);

public sealed record ZoneNodeSettings(
    string NodeId,
    string SpotRouterEndpoint,
    string SpotPubSubEndpoint,
    string OpsChannelEndpoint,
    string ActorsChannelEndpoint,
    string BridgeEndpoint,
    string? FaultTickZone = null,
    bool DisableBots = false)
{
    public RoutingId NodeRid => RoutingId.From(ZoneTopology.RidOf(NodeId));
}

public sealed record GatewaySettings(
    string StreamEndpoint,
    string SpotRouterEndpoint,
    string SpotPubSubEndpoint,
    string NodeRid)
{
    public RoutingId RoutingId => Systems.Zlink.RoutingId.From(NodeRid);
}

public sealed record OpsSettings(
    string StreamEndpoint,
    string BroadcastEndpoint,
    string ReportEndpoint);

public sealed record ZoneWorldClientSettings(
    string GatewayEndpoint,
    string OpsEndpoint,
    string Scenarios = "all");

public sealed record ZoneWorldConfiguration(
    ZoneWorldSettings Shared,
    ZoneNodeSettings? ZoneNode = null,
    GatewaySettings? Gateway = null,
    OpsSettings? Ops = null,
    ZoneWorldClientSettings? Client = null)
{
    public static ZoneWorldConfiguration Load(string[] args)
    {
        if (args.Length != 2 || args[0] != "--config")
            throw new ArgumentException("Usage: --config PATH");

        var configuration = new ConfigurationBuilder()
            .AddJsonFile(Path.GetFullPath(args[1]), optional: false, reloadOnChange: false)
            .Build()
            .Get<ZoneWorldConfiguration>()
            ?? throw new InvalidOperationException("ZoneWorld configuration is empty.");
        configuration.Validate();
        return configuration;
    }

    private void Validate()
    {
        Required(Shared.RedisEndpoint, nameof(Shared.RedisEndpoint));
        Required(Shared.RedisKeyPrefix, nameof(Shared.RedisKeyPrefix));
        Required(Shared.LogDirectory, nameof(Shared.LogDirectory));

        var roles = new object?[] { ZoneNode, Gateway, Ops, Client }.Count(value => value is not null);
        if (roles != 1)
            throw new InvalidOperationException("Exactly one ZoneWorld role must be configured.");
    }

    private static void Required(string value, string name)
    {
        if (string.IsNullOrWhiteSpace(value))
            throw new InvalidOperationException($"ZoneWorld configuration value '{name}' is required.");
    }
}
