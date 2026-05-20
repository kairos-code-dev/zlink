using Systems.Zlink;

namespace TicTacToe.SessionGateway.Infrastructure.Configuration;

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
            ReadEndpoint("TICTACTOE_SG_REGISTRY_PUB_ENDPOINT", "tcp://127.0.0.1:47201"),
            ReadEndpoint("TICTACTOE_SG_REGISTRY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47202"),
            ReadEndpoint("TICTACTOE_SG_API_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47203"),
            ReadEndpoint("TICTACTOE_SG_PLAY_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47204"),
            ReadEndpoint("TICTACTOE_SG_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47205"),
            ReadEndpoint("TICTACTOE_SG_RECONNECT_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47206"),
            ReadEndpoint("TICTACTOE_SG_PLAY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47207"),
            ReadEndpoint("TICTACTOE_SG_PLAY_SPOT_ENDPOINT", "tcp://127.0.0.1:47208"),
            ReadEndpoint("TICTACTOE_SG_STREAM_ENDPOINT", "tcp://127.0.0.1:47209"),
            ReadEndpoint("TICTACTOE_SG_RECONNECT_STREAM_ENDPOINT", "tcp://127.0.0.1:47210"),
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
