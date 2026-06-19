using Systems.Zlink;

namespace Bingo.Server.Configuration;

public sealed record SampleTopology(
    string RegistryPubEndpoint,
    string RegistryRouterEndpoint,
    SampleApiNode ApiA,
    SampleApiNode ApiB,
    SamplePlayNode PlayA,
    SamplePlayNode PlayB,
    SampleSessionNode SessionA,
    SampleSessionNode SessionB,
    string RedisEndpoint)
{
    public static SampleTopology Create()
    {
        var playAChannelEndpoint = ReadEndpoint("BINGO_PLAY_A_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47104");
        var playBChannelEndpoint = ReadEndpoint("BINGO_PLAY_B_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47114");
        var playA = new SamplePlayNode(
            playAChannelEndpoint,
            playBChannelEndpoint,
            ReadEndpoint("BINGO_PLAY_A_SPOT_ENDPOINT", "tcp://127.0.0.1:47110"),
            ReadEndpoint("BINGO_PLAY_A_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:47111"),
            RoutingId.From("2201"));
        var playB = new SamplePlayNode(
            playBChannelEndpoint,
            playAChannelEndpoint,
            ReadEndpoint("BINGO_PLAY_B_SPOT_ENDPOINT", "tcp://127.0.0.1:47115"),
            ReadEndpoint("BINGO_PLAY_B_SPOT_ROUTER_ENDPOINT", "tcp://127.0.0.1:47116"),
            RoutingId.From("2202"));

        return new SampleTopology(
            ReadEndpoint("BINGO_REGISTRY_PUB_ENDPOINT", "tcp://127.0.0.1:47101"),
            ReadEndpoint("BINGO_REGISTRY_ROUTER_ENDPOINT", "tcp://127.0.0.1:47102"),
            new SampleApiNode(
                ReadEndpoint("BINGO_API_A_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47103"),
                ReadEndpoint("BINGO_API_A_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47118"),
                RoutingId.From("3301")),
            new SampleApiNode(
                ReadEndpoint("BINGO_API_B_CHANNEL_ENDPOINT", "tcp://127.0.0.1:47117"),
                ReadEndpoint("BINGO_API_B_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47119"),
                RoutingId.From("3302")),
            playA,
            playB,
            new SampleSessionNode(
                ReadEndpoint("BINGO_SESSION_A_SPOT_ENDPOINT", "tcp://127.0.0.1:47105"),
                ReadEndpoint("BINGO_SESSION_A_ROUTER_ENDPOINT", "tcp://127.0.0.1:47106"),
                ReadEndpoint("BINGO_SESSION_A_STREAM_ENDPOINT", "tcp://127.0.0.1:47112"),
                ReadEndpoint("BINGO_SESSION_A_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47120"),
                RoutingId.From("1101"),
                RoutingId.From("1102"),
                RoutingId.From("1105"),
                playA.NodeRid,
                playA.PlayChannelEndpoint),
            new SampleSessionNode(
                ReadEndpoint("BINGO_SESSION_B_SPOT_ENDPOINT", "tcp://127.0.0.1:47107"),
                ReadEndpoint("BINGO_SESSION_B_ROUTER_ENDPOINT", "tcp://127.0.0.1:47108"),
                ReadEndpoint("BINGO_SESSION_B_STREAM_ENDPOINT", "tcp://127.0.0.1:47113"),
                ReadEndpoint("BINGO_SESSION_B_PLAY_ROUTE_ENDPOINT", "tcp://127.0.0.1:47121"),
                RoutingId.From("1103"),
                RoutingId.From("1104"),
                RoutingId.From("1106"),
                playB.NodeRid,
                playB.PlayChannelEndpoint),
            ReadEndpoint("BINGO_REDIS_ENDPOINT", "127.0.0.1:6379"));
    }

    public SampleApiNode Api(string nodeName)
    {
        return string.Equals(nodeName, "b", StringComparison.OrdinalIgnoreCase) ? ApiB : ApiA;
    }

    public SamplePlayNode Play(string nodeName)
    {
        return string.Equals(nodeName, "b", StringComparison.OrdinalIgnoreCase) ? PlayB : PlayA;
    }

    public SampleSessionNode Session(string nodeName)
    {
        return string.Equals(nodeName, "b", StringComparison.OrdinalIgnoreCase) ? SessionB : SessionA;
    }

    private static string ReadEndpoint(string name, string defaultValue)
    {
        var value = Environment.GetEnvironmentVariable(name);
        return string.IsNullOrWhiteSpace(value) ? defaultValue : value;
    }
}

public sealed record SampleApiNode(
    string ChannelEndpoint,
    string PlayRouteEndpoint,
    RoutingId RouteRid);

public sealed record SamplePlayNode(
    string PlayChannelEndpoint,
    string PeerPlayChannelEndpoint,
    string SpotPubEndpoint,
    string SpotRouterEndpoint,
    RoutingId NodeRid);

public sealed record SampleSessionNode(
    string PubEndpoint,
    string RouterEndpoint,
    string StreamEndpoint,
    string PlayRouteEndpoint,
    RoutingId RouterRoutingId,
    RoutingId PubRoutingId,
    RoutingId PlayRouteRid,
    RoutingId PreferredPlayNodeRid,
    string PreferredPlayChannelEndpoint);
