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
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            EphemeralTcpEndpoint.Create(),
            RoutingId.FromString("1101"),
            RoutingId.FromString("1102"),
            RoutingId.FromString("2202"));
    }
}

public sealed record SampleSessionNode(
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId);
