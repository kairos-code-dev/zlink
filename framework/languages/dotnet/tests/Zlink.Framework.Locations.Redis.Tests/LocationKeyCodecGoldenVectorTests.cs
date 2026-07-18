using Zlink.Framework.Runtime.Locations;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class LocationKeyCodecGoldenVectorTests
{
    public static TheoryData<ZLinkMeshNodeDescriptorKey, string> MeshNodeKeys => new()
    {
        {
            new ZLinkMeshNodeDescriptorKey("play", RoutingId.From("node-1")),
            "4:play12:6e6f64652d31"
        },
        {
            new ZLinkMeshNodeDescriptorKey("world", RoutingId.From("node-2")),
            "5:world12:6e6f64652d32"
        }
    };

    [Theory]
    [MemberData(nameof(MeshNodeKeys))]
    public void Framework_And_Redis_Codecs_Match_All_MeshNode_Key_Golden_Vectors(
        ZLinkMeshNodeDescriptorKey key,
        string expected)
    {
        Assert.Equal(expected, ZLinkLocationKeyCodec.EncodeMeshNodeKey(key));
        Assert.Equal(expected, ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(key));
    }

    [Fact]
    public void Spot_And_Actor_Keys_Match_The_Shared_Canonical_Form()
    {
        var spot = new ZLinkSpotLocationKey("play", RoutingId.From("spot-1"));
        Assert.Equal("4:play12:73706f742d31", ZLinkLocationKeyCodec.EncodeSpotKey(spot));
        Assert.Equal(
            ZLinkLocationKeyCodec.EncodeSpotKey(spot),
            ZLinkRedisLocationKeyCodec.EncodeSpotKey(spot));

        var actor = new ZLinkActorLocationKey("play", "actor-1");
        Assert.Equal("4:play7:actor-1", ZLinkLocationKeyCodec.EncodeActorKey(actor));
        Assert.Equal(
            ZLinkLocationKeyCodec.EncodeActorKey(actor),
            ZLinkRedisLocationKeyCodec.EncodeActorKey(actor));
    }

    [Fact]
    public void Length_Prefixes_Disambiguate_Adjacent_Segments()
    {
        var first = new ZLinkActorLocationKey("ab", "c");
        var second = new ZLinkActorLocationKey("a", "bc");

        Assert.Equal("2:ab1:c", ZLinkLocationKeyCodec.EncodeActorKey(first));
        Assert.Equal("1:a2:bc", ZLinkLocationKeyCodec.EncodeActorKey(second));
        Assert.NotEqual(
            ZLinkLocationKeyCodec.EncodeActorKey(first),
            ZLinkLocationKeyCodec.EncodeActorKey(second));
    }
}
