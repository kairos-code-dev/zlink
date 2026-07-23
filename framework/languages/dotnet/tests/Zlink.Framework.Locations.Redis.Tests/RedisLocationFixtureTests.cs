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
            SecurityIdentity: "cluster-a",
            OwnerId: "mesh-owner-a",
            LeaseGeneration: 9,
            UpdatedAt: FixtureUpdatedAt)
        {
            State = ZLinkFrameworkRuntimeState.Serving
        };

        Assert.Equal(
            RequiredString(row, "key"),
            ZLinkRedisLocationKeyCodec.EncodeMeshNodeKey(
                new ZLinkMeshNodeDescriptorKey(descriptor.MeshName, descriptor.Rid)));

        var physicalKeys = root.GetProperty("physicalKeys");
        var keys = new ZLinkRedisLocationKeys("P");
        var canonicalKey = RequiredString(row, "key");
        Assert.Equal(
            RequiredString(physicalKeys, "descriptor"),
            keys.HybridDescriptorKey(canonicalKey).ToString());
        Assert.Equal(
            RequiredString(physicalKeys, "admission"),
            keys.HybridDescriptorAdmissionKey(canonicalKey).ToString());
        Assert.Equal(
            RequiredString(physicalKeys, "ownerLease"),
            keys.HybridOwnerLeaseKey(descriptor.OwnerId).ToString());

        var hash = row.GetProperty("hash");
        Assert.Equal(descriptor.OwnerId, RequiredString(hash, "owner"));
        Assert.Equal(
            descriptor.LifecycleGeneration.ToString(System.Globalization.CultureInfo.InvariantCulture),
            RequiredString(hash, "gen"));
        Assert.Equal(descriptor.MeshName, RequiredString(hash, "mesh"));
        Assert.Equal(
            RequiredString(hash, "json"),
            ZLinkRedisLocationRowJson.Serialize(descriptor));
        Assert.Equal(
            RequiredString(
                root.GetProperty("immutableDigest"),
                "sha256LowerHex"),
            ZLinkRedisLocationCommands.ImmutableDescriptorDigest(
                descriptor));
    }

    [Fact]
    public void ClientServer_Descriptor_V1_Fixture_Matches_Current_Codec_Output()
    {
        using var document = JsonDocument.Parse(
            File.ReadAllText(FixturePath(
                "client-server-server-descriptor-v1.json")));
        var root = document.RootElement;
        Assert.Equal(
            "client-server-server-descriptor-v1",
            RequiredString(root, "format"));
        Assert.Equal(
            new[] { "owner", "gen", "json", "updatedAtMs", "channel" },
            ReadStringArray(root.GetProperty("hashFields")));

        var descriptor = new ZLinkClientServerServerDescriptor(
            "orders",
            RoutingId.From("orders-a"),
            LifecycleGeneration: 7,
            DescriptorRevision: 3,
            "tcp://10.0.0.2:7400",
            Weight: 100,
            ZLinkFrameworkRuntimeState.Serving,
            SecurityIdentity: "cluster-a",
            OwnerId: "channel-owner-a",
            LeaseGeneration: 5,
            UpdatedAt: FixtureUpdatedAt);
        var row = root.GetProperty("row");
        Assert.Equal(
            RequiredString(row, "key"),
            ZLinkRedisLocationKeyCodec.EncodeClientServerKey(
                new ZLinkClientServerServerDescriptorKey(
                    descriptor.ChannelName,
                    descriptor.ServerRid)));

        var expectedJson = RequiredString(
            row.GetProperty("hash"),
            "json");
        Assert.Equal(
            expectedJson,
            ZLinkRedisLocationRowJson.Serialize(descriptor));
        Assert.Equal(
            descriptor,
            ZLinkRedisLocationRowJson
                .Deserialize<ZLinkClientServerServerDescriptor>(expectedJson));
    }

    [Fact]
    public void Authority_Store_V1_Fixture_Uses_Hybrid_Physical_Schema()
    {
        using var document = JsonDocument.Parse(
            File.ReadAllText(FixturePath("authority-store-v1.json")));
        var root = document.RootElement;

        Assert.Equal(
            "location-authority-hybrid-v1",
            RequiredString(root, "format"));
        Assert.False(root.TryGetProperty("newObject", out _));

        var keyContract = root.GetProperty("keyContract");
        var canonicalKey = RequiredString(keyContract, "authorityKey");
        var keys = new ZLinkRedisLocationKeys("P");
        Assert.Equal(
            RequiredString(keyContract, "currentKey"),
            keys.HybridAuthorityCurrentKey(canonicalKey).ToString());
        Assert.Equal(
            RequiredString(keyContract, "historyKey"),
            keys.HybridAuthorityHistoryKey(canonicalKey).ToString());
        Assert.Equal(
            RequiredString(keyContract, "historyRevisionKey"),
            keys.HybridAuthorityHistoryRevisionsKey(canonicalKey).ToString());
        Assert.Equal(
            RequiredString(keyContract, "indexKey"),
            keys.HybridAuthorityKeyIndexKey().ToString());

        Assert.Equal(
            new[]
            {
                "authorityKey",
                "payload",
                "storeVersion",
                "objectGeneration",
                "authorityOwnerGeneration",
                "ownerId",
                "ownerLeaseGeneration",
                "allocationState",
                "objectKind",
                "stableType",
                "descriptorKey",
                "descriptorLifecycleGeneration",
                "capacityDelta"
            },
            ReadStringArray(root.GetProperty("currentHashFields")));

        var capacity = root.GetProperty("capacityBuckets");
        var descriptorKey = RequiredString(capacity, "descriptorKey");
        var lifecycle = ulong.Parse(
            RequiredString(capacity, "descriptorLifecycleGeneration"),
            System.Globalization.CultureInfo.InvariantCulture);
        Assert.Equal(
            RequiredString(capacity, "node"),
            ZLinkRedisLocationKeys.HybridCapacityNodeBucket(
                descriptorKey,
                lifecycle));
        Assert.Equal(
            RequiredString(capacity, "type"),
            ZLinkRedisLocationKeys.HybridCapacityTypeBucket(
                descriptorKey,
                lifecycle,
                ZLinkPlacementObjectKind.UserSpot,
                RequiredString(capacity, "stableType")));
        Assert.Equal(
            RequiredString(capacity, "unicodeType"),
            ZLinkRedisLocationKeys.HybridCapacityTypeBucket(
                descriptorKey,
                lifecycle,
                ZLinkPlacementObjectKind.UserSpot,
                RequiredString(capacity, "unicodeStableType")));
    }

    [Fact]
    public void Actor_Location_V2_Fixture_Matches_Current_Codec_Output()
    {
        using var document = JsonDocument.Parse(File.ReadAllText(FixturePath("actor-location-v2.json")));
        var root = document.RootElement;

        Assert.Equal("actor-location-v2", root.GetProperty("format").GetString());
        Assert.Equal(
            new[]
            {
                "payload",
                "storeVersion",
                "objectGeneration",
                "authorityOwnerGeneration",
                "ownerId",
                "ownerLeaseGeneration"
            },
            ReadStringArray(root.GetProperty("hashFields")));

        var row = root.GetProperty("row");
        Assert.Equal("actor", RequiredString(row, "kind"));
        Assert.Equal(
            root.GetProperty("keyContract").GetProperty("authorityKey").GetString(),
            RequiredString(row, "key"));
        var hash = row.GetProperty("hash");
        Assert.Equal("opaque-actor-authority-v1", RequiredString(hash, "payload"));
        Assert.Equal("101", RequiredString(hash, "storeVersion"));
        Assert.Equal("11", RequiredString(hash, "objectGeneration"));
        Assert.Equal("4", RequiredString(hash, "authorityOwnerGeneration"));
        Assert.Equal("actor-owner-a", RequiredString(hash, "ownerId"));
        Assert.Equal("9", RequiredString(hash, "ownerLeaseGeneration"));
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
