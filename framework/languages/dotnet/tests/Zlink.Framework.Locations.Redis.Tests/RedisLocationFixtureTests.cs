using System.Text.Json;

namespace Zlink.Framework.Locations.Redis.Tests;

public sealed class RedisLocationFixtureTests
{
    private static readonly DateTimeOffset FixtureUpdatedAt =
        new(2024, 7, 15, 0, 0, 0, TimeSpan.Zero);

    [Fact]
    public void MeshNode_Descriptor_V1_Fixture_Matches_Current_Codec_Output()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(FixturePath("mesh-node-descriptor-v1.json")));
        var root = document.RootElement;

        Assert.Equal("mesh-node-descriptor-v1", root.GetProperty("format").GetString());
        Assert.Equal(
            new[] { "owner", "gen", "json", "updatedAtMs", "mesh" },
            ReadStringArray(root.GetProperty("hashFields")));

        var row = root.GetProperty("row");
        Assert.Equal("mesh", RequiredString(row, "kind"));

        var descriptor = new ZLinkMeshNodeDescriptor(
            "game",
            RoutingId.From("game-a"),
            LifecycleGeneration: 7,
            DescriptorRevision: 3,
            "tcp://10.0.0.1:7300",
            new Dictionary<string, int>(StringComparer.Ordinal) { ["orders"] = 100, ["world"] = 50 },
            Draining: false,
            SecurityIdentity: "cluster-a",
            OwnerId: "mesh-owner-a",
            UpdatedAt: FixtureUpdatedAt);

        Assert.Equal(
            RequiredString(row, "key"),
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(descriptor.MeshName, descriptor.Rid)));

        var hash = row.GetProperty("hash");
        Assert.Equal(descriptor.OwnerId, RequiredString(hash, "owner"));
        Assert.Equal(descriptor.MeshName, RequiredString(hash, "mesh"));
        Assert.Equal(
            RequiredString(hash, "json"),
            ZLinkRedisLocationRowJson.Serialize(descriptor));
    }

    [Fact]
    public void Actor_Location_V2_Fixture_Matches_Current_Codec_Output()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(FixturePath("actor-location-v2.json")));
        var root = document.RootElement;

        Assert.Equal("actor-location-v2", root.GetProperty("format").GetString());
        Assert.Equal(
            new[] { "owner", "gen", "json", "updatedAtMs", "mesh" },
            ReadStringArray(root.GetProperty("hashFields")));

        var row = root.GetProperty("row");
        Assert.Equal("actor", RequiredString(row, "kind"));

        var actor = new ZLinkActorLocation(
            "game",
            "actor-1",
            "player",
            new ActorRef(RoutingId.From("game-a"), "actor-1", 11),
            OwnerNodeRid: RoutingId.From("game-a"),
            OwnerNodeGeneration: 7,
            SpotRid: RoutingId.From("spot-1"),
            SpotGeneration: 3,
            SpotKind: ZLinkSpotKind.User,
            MembershipEpoch: 4,
            OwnerId: "actor-owner-a",
            UpdatedAt: FixtureUpdatedAt);

        Assert.Equal(
            RequiredString(row, "key"),
            ZLinkRedisLocationKeyCodec.EncodeActorKey(
                new ZLinkActorLocationKey(actor.MeshName, actor.ActorId)));

        var hash = row.GetProperty("hash");
        Assert.Equal(actor.OwnerId, RequiredString(hash, "owner"));
        Assert.Equal(actor.MeshName, RequiredString(hash, "mesh"));
        var json = RequiredString(hash, "json");
        Assert.Equal(json, ZLinkRedisLocationRowJson.Serialize(actor));

        // The actor ref is the typed camelCase object, never a string format.
        Assert.Contains(
            "\"ActorRef\":{\"nodeRid\":\"67616d652d61\",\"actorId\":\"actor-1\",\"generation\":11}",
            json,
            StringComparison.Ordinal);
        Assert.Contains("\"SpotKind\":2", json, StringComparison.Ordinal);
        Assert.DoesNotContain("LocationKind", json, StringComparison.Ordinal);
    }

    private static string FixturePath(string fileName)
    {
        foreach (var root in new[] { AppContext.BaseDirectory, Environment.CurrentDirectory })
        {
            var directory = new DirectoryInfo(root);
            while (directory is not null)
            {
                var candidate = Path.Combine(
                    directory.FullName,
                    "framework",
                    "testdata",
                    "location",
                    "redis",
                    fileName);
                if (File.Exists(candidate))
                {
                    return candidate;
                }

                directory = directory.Parent;
            }
        }

        throw new FileNotFoundException($"Could not find framework/testdata/location/redis/{fileName}.");
    }

    private static string RequiredString(JsonElement element, string propertyName) =>
        element.GetProperty(propertyName).GetString()
        ?? throw new InvalidOperationException($"Fixture property '{propertyName}' is null.");

    private static string[] ReadStringArray(JsonElement element) =>
        element.EnumerateArray().Select(item => item.GetString() ?? string.Empty).ToArray();
}
