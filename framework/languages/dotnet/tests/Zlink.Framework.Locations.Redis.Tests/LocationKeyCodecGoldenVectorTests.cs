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
        var spot = new ZLinkSpotLocationKey("spot-1");
        Assert.Equal("6:spot-1", ZLinkLocationKeyCodec.EncodeSpotKey(spot));
        Assert.Equal(
            ZLinkLocationKeyCodec.EncodeSpotKey(spot),
            ZLinkRedisLocationKeyCodec.EncodeSpotKey(spot));

        var actor = new ZLinkActorLocationKey("actor-1");
        Assert.Equal("7:actor-1", ZLinkLocationKeyCodec.EncodeActorKey(actor));
        Assert.Equal(
            ZLinkLocationKeyCodec.EncodeActorKey(actor),
            ZLinkRedisLocationKeyCodec.EncodeActorKey(actor));
    }

    [Fact]
    public void Spot_Key_Is_Global_And_Validates_The_Utf8_Contract()
    {
        Assert.Equal(
            "6:spot-1",
            ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey("spot-1")));
        Assert.Equal(
            "85:" + new string('가', 85),
            ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey(new string('가', 85))));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey(string.Empty)));
        Assert.Throws<ArgumentOutOfRangeException>(
            () => ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey(new string('a', 256))));
        Assert.Throws<ArgumentException>(
            () => ZLinkLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey("\ud800")));
    }

    [Fact]
    public void Actor_Key_Uses_The_Global_Actor_Id()
    {
        var first = new ZLinkActorLocationKey("c");
        var second = new ZLinkActorLocationKey("bc");

        Assert.Equal("1:c", ZLinkLocationKeyCodec.EncodeActorKey(first));
        Assert.Equal("2:bc", ZLinkLocationKeyCodec.EncodeActorKey(second));
        Assert.NotEqual(
            ZLinkLocationKeyCodec.EncodeActorKey(first),
            ZLinkLocationKeyCodec.EncodeActorKey(second));
    }

    [Fact]
    public void Creation_Terminal_Key_Uses_Exact_RoutingId_Bytes_And_Operation()
    {
        var keys = new ZLinkRedisLocationKeys("P");
        var operation = new ZLinkCreationOperationId(
            RoutingId.From("node-a"),
            7,
            0,
            1);

        Assert.Equal(
            "P:{zlink-location-v3}:creation-terminal:"
            + "6:6e6f64652d61:7:00000000000000000000000000000001",
            keys.HybridCreationTerminalKey(operation).ToString());
    }
}
