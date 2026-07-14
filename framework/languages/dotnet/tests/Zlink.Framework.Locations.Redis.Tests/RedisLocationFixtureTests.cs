using System.Text.Json;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class RedisLocationFixtureTests
{
    private const string OwnerA = "owner-a";

    [Fact]
    public void Actor_Location_V2_Fixture_Matches_Current_Codec_Output()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(FixturePath()));
        var root = document.RootElement;

        Assert.Equal(
            "All four Redis extensions must match this fixture byte-for-byte. Canonical contract: framework/doc/framework/spec/location-store-redis.ko.md.",
            root.GetProperty("notice").GetString());
        Assert.Equal("actor-location-v2", root.GetProperty("format").GetString());
        Assert.Equal(
            new[] { "owner", "gen", "json", "updatedAtMs" },
            ReadStringArray(root.GetProperty("hashFields")));

        var rows = root.GetProperty("rows").EnumerateArray().ToDictionary(
            row => RequiredString(row, "kind"),
            row => row);

        AssertRow(
            rows["actor"],
            "7:actor-1",
            ZLinkRedisLocationRowJson.Serialize(TestRows.Actor(OwnerA, 0)));
        AssertRow(
            rows["peer"],
            "10:route-mesh4:play6:router12:6e6f64652d31",
            ZLinkRedisLocationRowJson.Serialize(TestRows.Peer(OwnerA)));
        AssertRow(
            rows["spot"],
            "4:play12:73706f742d31",
            ZLinkRedisLocationRowJson.Serialize(TestRows.Spot(OwnerA, "spot-1")));
        AssertRow(
            rows["route"],
            "1:17:route-1",
            ZLinkRedisLocationRowJson.Serialize(TestRows.Route(OwnerA)));

        var actorJson = RequiredString(rows["actor"].GetProperty("hash"), "json");
        Assert.Contains("\"ActorRef\":{\"nodeRid\":\"6e6f64652d31\",\"actorId\":\"actor-1\",\"generation\":1}", actorJson, StringComparison.Ordinal);
        Assert.Contains("\"LocationKind\":1", actorJson, StringComparison.Ordinal);
        Assert.DoesNotContain("SpotKind", actorJson, StringComparison.Ordinal);
    }

    [Fact]
    public void Actor_Location_V2_Fixture_Keys_Match_Current_Key_Codec_Output()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(FixturePath()));
        var rows = document.RootElement.GetProperty("rows").EnumerateArray().ToDictionary(
            row => RequiredString(row, "kind"),
            row => row);

        var actor = TestRows.Actor(OwnerA, 0);
        var peer = TestRows.Peer(OwnerA);
        var spot = TestRows.Spot(OwnerA, "spot-1");
        var route = TestRows.Route(OwnerA);

        Assert.Equal(
            ZLinkRedisLocationKeyCodec.EncodeActorKey(new ZLinkActorLocationKey(actor.ActorId)),
            RequiredString(rows["actor"], "key"));
        Assert.Equal(
            ZLinkRedisLocationKeyCodec.EncodePeerKey(new ZLinkPeerLocationKey(
                peer.AutoConnectType,
                peer.MeshName,
                peer.Role,
                peer.NodeRid,
                peer.Endpoint)),
            RequiredString(rows["peer"], "key"));
        Assert.Equal(
            ZLinkRedisLocationKeyCodec.EncodeSpotKey(new ZLinkSpotLocationKey(spot.MeshName, spot.SpotRid)),
            RequiredString(rows["spot"], "key"));
        Assert.Equal(
            ZLinkRedisLocationKeyCodec.EncodeRouteKey(new ZLinkRouteLocationKey(route.RouteKind, route.RouteKey)),
            RequiredString(rows["route"], "key"));
    }

    private static void AssertRow(JsonElement row, string expectedKey, string expectedJson)
    {
        Assert.Equal(expectedKey, RequiredString(row, "key"));
        var hash = row.GetProperty("hash");
        Assert.Equal(OwnerA, RequiredString(hash, "owner"));
        Assert.Equal("0", RequiredString(hash, "gen"));
        Assert.Equal(expectedJson, RequiredString(hash, "json"));
        Assert.Equal("0", RequiredString(hash, "updatedAtMs"));
    }

    private static string FixturePath()
    {
        foreach (var root in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            var directory = new DirectoryInfo(root);
            while (directory is not null)
            {
                var candidate = Path.Combine(
                    directory.FullName,
                    "testdata",
                    "location",
                    "redis",
                    "actor-location-v2.json");
                if (File.Exists(candidate))
                {
                    return candidate;
                }

                directory = directory.Parent;
            }
        }

        throw new FileNotFoundException("Could not find testdata/location/redis/actor-location-v2.json.");
    }

    private static string RequiredString(JsonElement element, string propertyName) =>
        element.GetProperty(propertyName).GetString()
        ?? throw new InvalidOperationException($"Fixture property '{propertyName}' is null.");

    private static string[] ReadStringArray(JsonElement element) =>
        element.EnumerateArray().Select(item => item.GetString() ?? string.Empty).ToArray();
}
