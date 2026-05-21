using Systems.Zlink;

namespace Bingo.Shared.Configuration;

public sealed record SampleTopology(
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint,
    string ApiChannelEndpoint,
    string PlayChannelEndpoint,
    string SessionRouterEndpoint,
    string ReconnectSessionRouterEndpoint,
    string PlayRouterEndpoint,
    string PlaySpotEndpoint,
    string StreamEndpoint,
    string ReconnectStreamEndpoint,
    RoutingId SessionRid,
    RoutingId ReconnectSessionRid,
    RoutingId PlayRid)
{
    public SampleSessionNode PrimarySession => new(
        SessionRouterEndpoint,
        StreamEndpoint,
        SessionRid);

    public SampleSessionNode ReconnectSession => new(
        ReconnectSessionRouterEndpoint,
        ReconnectStreamEndpoint,
        ReconnectSessionRid);

    public static SampleTopology Create()
    {
        return new SampleTopology(
            ReadEndpoint("BINGO_REGISTRY_PUB_ENDPOINT", "tcp://127.0.0.1:47101"),
            ReadEndpoint("BINGO_REGISTRY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47102"),
            ReadEndpoint("BINGO_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47103"),
            ReadEndpoint("BINGO_PLAY_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47104"),
            ReadEndpoint("BINGO_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47105"),
            ReadEndpoint("BINGO_RECONNECT_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47106"),
            ReadEndpoint("BINGO_PLAY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47107"),
            ReadEndpoint("BINGO_PLAY_SPOT_ENDPOINT", "tcp://127.0.0.1:47108"),
            ReadEndpoint("BINGO_STREAM_ENDPOINT", "tcp://127.0.0.1:47109"),
            ReadEndpoint("BINGO_RECONNECT_STREAM_ENDPOINT", "tcp://127.0.0.1:47110"),
            RoutingId.FromString("1101"),
            RoutingId.FromString("1102"),
            RoutingId.FromString("2202"));
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}

public sealed record SampleSessionNode(
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId);
