using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class LocationKeyCodecGoldenVectorTests
{
    public static TheoryData<ZLinkPeerLocationKey, string> PeerKeys => new()
    {
        {
            Peer(
                ZLinkLocationAutoConnectType.RouteMesh,
                ZLinkLocationRole.Router,
                "play",
                "node-1",
                "tcp://ignored-when-node-rid-exists"),
            "10:route-mesh4:play6:router12:6e6f64652d31"
        },
        {
            Peer(ZLinkLocationAutoConnectType.ClientServer, ZLinkLocationRole.Dealer, "api", null, "tcp://api"),
            "13:client-server3:api6:dealer9:tcp://api"
        },
        {
            Peer(ZLinkLocationAutoConnectType.DealerMesh, ZLinkLocationRole.Dealer, "jobs", "node-2", null),
            "11:dealer-mesh4:jobs6:dealer12:6e6f64652d32"
        },
        {
            Peer(ZLinkLocationAutoConnectType.Fanout, ZLinkLocationRole.Pub, "events", null, "tcp://pub"),
            "6:fanout6:events3:pub9:tcp://pub"
        },
        {
            Peer(ZLinkLocationAutoConnectType.Fanout, ZLinkLocationRole.Sub, "events", null, "tcp://sub"),
            "6:fanout6:events3:sub9:tcp://sub"
        },
        {
            Peer(ZLinkLocationAutoConnectType.SpotMesh, ZLinkLocationRole.Spot, "world", "spot-1", null),
            "9:spot-mesh5:world4:spot12:73706f742d31"
        }
    };

    [Theory]
    [MemberData(nameof(PeerKeys))]
    public void Framework_And_Redis_Codecs_Match_All_Peer_Key_Golden_Vectors(
        ZLinkPeerLocationKey key,
        string expected)
    {
        Assert.Equal(expected, ZLinkLocationKeyCodec.EncodePeerKey(key));
        Assert.Equal(expected, ZLinkRedisLocationKeyCodec.EncodePeerKey(key));
    }

    [Fact]
    public void Length_Prefixes_Disambiguate_Adjacent_Peer_Segments()
    {
        var first = Peer(
            ZLinkLocationAutoConnectType.ClientServer,
            ZLinkLocationRole.Dealer,
            "ab",
            null,
            "c");
        var second = Peer(
            ZLinkLocationAutoConnectType.ClientServer,
            ZLinkLocationRole.Dealer,
            "a",
            null,
            "bc");

        Assert.Equal("13:client-server2:ab6:dealer1:c", ZLinkLocationKeyCodec.EncodePeerKey(first));
        Assert.Equal("13:client-server1:a6:dealer2:bc", ZLinkLocationKeyCodec.EncodePeerKey(second));
        Assert.Equal(
            ZLinkLocationKeyCodec.EncodePeerKey(first),
            ZLinkRedisLocationKeyCodec.EncodePeerKey(first));
        Assert.Equal(
            ZLinkLocationKeyCodec.EncodePeerKey(second),
            ZLinkRedisLocationKeyCodec.EncodePeerKey(second));
        Assert.NotEqual(
            ZLinkLocationKeyCodec.EncodePeerKey(first),
            ZLinkLocationKeyCodec.EncodePeerKey(second));
    }

    private static ZLinkPeerLocationKey Peer(
        ZLinkLocationAutoConnectType type,
        ZLinkLocationRole role,
        string mesh,
        string? nodeRid,
        string? endpoint) =>
        new(type, mesh, role, nodeRid is null ? null : RoutingId.From(nodeRid), endpoint);
}
