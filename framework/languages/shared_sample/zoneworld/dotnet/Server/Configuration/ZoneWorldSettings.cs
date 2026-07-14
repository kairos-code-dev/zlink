using Systems.Zlink;

namespace ZoneWorld.Server.Configuration;

/// <summary>
/// Endpoints and identities, read from the environment so the runner can place every role
/// on its own ports. Peers find each other through the location store, so no server is
/// configured with another server's address (§3).
/// </summary>
public sealed record ZoneWorldSettings(
    string RedisEndpoint,
    string RedisKeyPrefix,
    string LogDirectory)
{
    public static ZoneWorldSettings Create() => new(
        Required("ZONEWORLD_REDIS_ENDPOINT"),
        Text("ZONEWORLD_REDIS_KEY_PREFIX", "zoneworld:"),
        Text("ZONEWORLD_LOG_DIR", Path.Combine(Directory.GetCurrentDirectory(), "logs")));

    public static string Text(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }

    public static string Required(string name)
    {
        var value = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrWhiteSpace(value))
            throw new InvalidOperationException(
                $"Environment variable '{name}' is required. Run the sample through run_sample.sh, "
                + "which provisions an isolated Redis and passes its endpoint to every role.");

        return value;
    }
}

/// <summary>The ports and routing ids one ZoneNode binds.</summary>
public sealed record ZoneNodeSettings(
    string NodeId,
    string SpotRouterEndpoint,
    string SpotPubSubEndpoint,
    string OpsChannelEndpoint,
    string ActorsChannelEndpoint,
    string BridgeEndpoint,
    RoutingId NodeRid,
    RoutingId? PeerNodeRid,
    string? PeerSpotRouterEndpoint,
    string? PeerSpotPubSubEndpoint,
    string? PeerBridgeEndpoint)
{
    public static ZoneNodeSettings Create(string nodeId)
    {
        var index = nodeId switch
        {
            "zone-node-1" => 1,
            "zone-node-2" => 2,
            "zone-node-3" => 3,
            _ => throw new ArgumentOutOfRangeException(nameof(nodeId), nodeId, "Unknown node id.")
        };
        var basePort = 48100 + index * 10;

        // The two zone nodes transfer actors to each other, so each one dials the other's
        // spot router. zone-node-3 hosts no zone and takes part in no transfer.
        var peer = index switch { 1 => 2, 2 => 1, _ => 0 };
        var peerBase = 48100 + peer * 10;

        return new ZoneNodeSettings(
            nodeId,
            ZoneWorldSettings.Text($"ZONEWORLD_NODE{index}_SPOT_ROUTER", $"tcp://127.0.0.1:{basePort}"),
            ZoneWorldSettings.Text($"ZONEWORLD_NODE{index}_SPOT_PUBSUB", $"tcp://127.0.0.1:{basePort + 1}"),
            ZoneWorldSettings.Text($"ZONEWORLD_NODE{index}_OPS_CHANNEL", $"tcp://127.0.0.1:{basePort + 2}"),
            ZoneWorldSettings.Text($"ZONEWORLD_NODE{index}_ACTORS_CHANNEL", $"tcp://127.0.0.1:{basePort + 3}"),
            ZoneWorldSettings.Text($"ZONEWORLD_NODE{index}_BRIDGE", $"tcp://127.0.0.1:{basePort + 4}"),
            RoutingId.From($"zn{index}"),
            peer == 0 ? null : RoutingId.From($"zn{peer}"),
            peer == 0 ? null : ZoneWorldSettings.Text($"ZONEWORLD_NODE{peer}_SPOT_ROUTER", $"tcp://127.0.0.1:{peerBase}"),
            peer == 0 ? null : ZoneWorldSettings.Text($"ZONEWORLD_NODE{peer}_SPOT_PUBSUB", $"tcp://127.0.0.1:{peerBase + 1}"),
            peer == 0 ? null : ZoneWorldSettings.Text($"ZONEWORLD_NODE{peer}_BRIDGE", $"tcp://127.0.0.1:{peerBase + 4}"));
    }
}

public sealed record GatewaySettings(
    string StreamEndpoint,
    string SpotRouterEndpoint,
    string SpotPubSubEndpoint,
    RoutingId NodeRid)
{
    public static GatewaySettings Create() => new(
        ZoneWorldSettings.Text("ZONEWORLD_GATEWAY_STREAM", "ws://127.0.0.1:48080"),
        ZoneWorldSettings.Text("ZONEWORLD_GATEWAY_SPOT_ROUTER", "tcp://127.0.0.1:48081"),
        ZoneWorldSettings.Text("ZONEWORLD_GATEWAY_SPOT_PUBSUB", "tcp://127.0.0.1:48082"),
        RoutingId.From("gw01"));
}

public sealed record OpsSettings(
    string StreamEndpoint,
    string BroadcastEndpoint,
    string ReportEndpoint)
{
    public static OpsSettings Create() => new(
        ZoneWorldSettings.Text("ZONEWORLD_OPS_STREAM", "ws://127.0.0.1:48090"),
        ZoneWorldSettings.Text("ZONEWORLD_OPS_BROADCAST", "tcp://127.0.0.1:48091"),
        ZoneWorldSettings.Text("ZONEWORLD_OPS_REPORT", "tcp://127.0.0.1:48092"));
}
