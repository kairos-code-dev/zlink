using TicTacToe.SessionGateway.Infrastructure;
using Zlink;

namespace TicTacToe.SessionGateway.Configuration;

internal sealed record SampleTopology(
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint,
    string SessionRouterEndpoint,
    string ReconnectSessionRouterEndpoint,
    string ApiRouterEndpoint,
    string PlayRouterEndpoint,
    string StreamEndpoint,
    string ReconnectStreamEndpoint,
    RoutingId SessionRid,
    RoutingId ReconnectSessionRid,
    RoutingId ApiRid,
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
            RoutingId.FromString("1101"),
            RoutingId.FromString("1102"),
            RoutingId.FromString("1202"),
            RoutingId.FromString("2202"));
    }
}

internal sealed record SampleSessionNode(
    string RouterEndpoint,
    string StreamEndpoint,
    RoutingId RoutingId);
