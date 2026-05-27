using Systems.Zlink;

namespace TicTacToe.SessionGateway.Shared.Configuration;

public sealed record SampleTopology(
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint,
    string ApiChannelEndpoint,
    string PlayChannelEndpoint,
    string SessionSpotEndpoint,
    string SessionRouterEndpoint,
    string ReconnectSessionSpotEndpoint,
    string ReconnectSessionRouterEndpoint,
    string PlayRouterEndpoint,
    string PlaySpotEndpoint,
    string PlaySpotRouterEndpoint,
    string StreamEndpoint,
    string ReconnectStreamEndpoint,
    RoutingId SessionRid,
    RoutingId ReconnectSessionRid,
    RoutingId PlayRid)
{
    public SampleSessionNode PrimarySession => new(
        SessionSpotEndpoint,
        SessionRouterEndpoint,
        StreamEndpoint,
        SessionRid);

    public SampleSessionNode ReconnectSession => new(
        ReconnectSessionSpotEndpoint,
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
            ReadEndpoint("TICTACTOE_SG_SESSION_SPOT_ENDPOINT", "tcp://127.0.0.1:47205"),
            ReadEndpoint("TICTACTOE_SG_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47206"),
            ReadEndpoint("TICTACTOE_SG_RECONNECT_SESSION_SPOT_ENDPOINT", "tcp://127.0.0.1:47207"),
            ReadEndpoint("TICTACTOE_SG_RECONNECT_SESSION_ROUTER_ENDPOINT", "tcp://127.0.0.1:47208"),
            ReadEndpoint("TICTACTOE_SG_PLAY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47209"),
            ReadEndpoint("TICTACTOE_SG_PLAY_SPOT_ENDPOINT", "tcp://127.0.0.1:47210"),
            ReadEndpoint("TICTACTOE_SG_PLAY_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:47211"),
            ReadEndpoint("TICTACTOE_SG_STREAM_ENDPOINT", "tcp://127.0.0.1:47212"),
            ReadEndpoint("TICTACTOE_SG_RECONNECT_STREAM_ENDPOINT", "tcp://127.0.0.1:47213"),
            RoutingId.From("1101"),
            RoutingId.From("1102"),
            RoutingId.From("2202"));
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}

public sealed record SampleSessionNode(
    string SpotEndpoint,
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId);
