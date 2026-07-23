using System.Text.Json;
using System.Text.Json.Serialization;
using Zlink.Framework.Contracts.Configuration;

namespace Zlink.Framework.Locations.Redis;

/// <summary>
/// JSON payload codec for location rows. The full record is stored in the
/// row hash's "json" field; the store-issued generation and store-clock
/// update time live in separate hash fields the Lua scripts maintain, and
/// they overwrite the record fields when a row is loaded.
/// </summary>
internal static class ZLinkRedisLocationRowJson
{
    private static readonly JsonSerializerOptions Options = new()
    {
        Converters =
        {
            new RoutingIdJsonConverter(),
            new ActorRefJsonConverter(),
            new ChannelWeightsJsonConverter(),
            new StringSetJsonConverter(),
            new ObjectCapabilitiesJsonConverter()
        }
    };

    internal static string Serialize<TRow>(TRow row)
    {
        if (row is ZLinkMeshNodeDescriptor descriptor)
            ValidateDescriptor(descriptor);
        var json = row is ZLinkClientServerServerDescriptor clientServer
            ? JsonSerializer.Serialize(
                ClientServerDescriptorJson.From(clientServer),
                Options)
            : JsonSerializer.Serialize(row, Options);
        if (System.Text.Encoding.UTF8.GetByteCount(json) > 1024 * 1024)
            throw new JsonException(
                "Location row JSON must not exceed 1 MiB.");
        return json;
    }

    internal static TRow Deserialize<TRow>(string json)
    {
        if (typeof(TRow) == typeof(ZLinkMeshNodeDescriptor))
            RequireCompleteDescriptor(json);
        if (typeof(TRow) == typeof(ZLinkClientServerServerDescriptor))
        {
            var encoded = JsonSerializer.Deserialize<ClientServerDescriptorJson>(
                              json,
                              Options)
                          ?? throw new InvalidOperationException(
                              "ClientServer descriptor payload deserialized to null.");
            return (TRow)(object)encoded.ToDescriptor();
        }

        var row = JsonSerializer.Deserialize<TRow>(json, Options)
                  ?? throw new InvalidOperationException(
                      "Location row payload deserialized to null.");
        if (row is ZLinkMeshNodeDescriptor descriptor)
            ValidateDescriptor(descriptor);
        return row;
    }

    private sealed record ClientServerDescriptorJson(
        string ChannelName,
        RoutingId ServerRid,
        ulong LifecycleGeneration,
        ulong DescriptorRevision,
        string Endpoint,
        int Weight,
        [property: JsonConverter(typeof(JsonStringEnumConverter))]
        ZLinkFrameworkRuntimeState State,
        string SecurityIdentity,
        string OwnerId,
        long OwnerLeaseGeneration,
        DateTimeOffset UpdatedAt)
    {
        internal static ClientServerDescriptorJson From(
            ZLinkClientServerServerDescriptor descriptor) =>
            new(
                descriptor.ChannelName,
                descriptor.ServerRid,
                descriptor.LifecycleGeneration,
                descriptor.DescriptorRevision,
                descriptor.Endpoint,
                descriptor.Weight,
                descriptor.State,
                descriptor.SecurityIdentity,
                descriptor.OwnerId,
                descriptor.LeaseGeneration,
                descriptor.UpdatedAt);

        internal ZLinkClientServerServerDescriptor ToDescriptor() =>
            new(
                ChannelName,
                ServerRid,
                LifecycleGeneration,
                DescriptorRevision,
                Endpoint,
                Weight,
                State,
                SecurityIdentity,
                OwnerId,
                OwnerLeaseGeneration,
                UpdatedAt);
    }

    private static void RequireCompleteDescriptor(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object)
            throw new JsonException(
                "MeshNode descriptor row must be a JSON object.");
        var fields = document.RootElement.EnumerateObject()
            .Select(static property => property.Name)
            .ToHashSet(StringComparer.OrdinalIgnoreCase);
        string[] required =
        [
            "MeshName",
            "Rid",
            "LifecycleGeneration",
            "DescriptorRevision",
            "Endpoint",
            "ChannelWeights",
            "SecurityIdentity",
            "OwnerId",
            "LeaseGeneration",
            "UpdatedAt",
            "ApplicationVersion",
            "ObjectCapabilities",
            "MaintenanceWave",
            "State",
            "ObjectRole",
            "PlacementWeight",
            "Capacity"
        ];
        var missing = required.Where(field => !fields.Contains(field)).ToArray();
        if (missing.Length != 0)
        {
            throw new JsonException(
                "MeshNode descriptor row is missing required fields: "
                + string.Join(", ", missing));
        }
    }

    private static void ValidateDescriptor(ZLinkMeshNodeDescriptor descriptor)
    {
        ArgumentNullException.ThrowIfNull(descriptor);
        RequireUtf8Value(descriptor.MeshName, nameof(descriptor.MeshName));
        if (descriptor.Rid.IsEmpty
            || descriptor.LifecycleGeneration == 0
            || descriptor.DescriptorRevision == 0
            || descriptor.ApplicationVersion < 0
            || descriptor.ChannelWeights is null
            || string.IsNullOrWhiteSpace(descriptor.OwnerId)
            || descriptor.LeaseGeneration <= 0
            || !Enum.IsDefined(descriptor.State)
            || !Enum.IsDefined(descriptor.ObjectRole)
            || descriptor.PlacementWeight is < 0 or > 100)
            throw new JsonException("MeshNode descriptor identity or placement fields are invalid.");
        if (descriptor.MaintenanceWave is { } maintenanceWave)
            RequireUtf8Value(
                maintenanceWave,
                nameof(descriptor.MaintenanceWave));
        if (descriptor.Capacity is not
            {
                Active: >= 0,
                Pending: >= 0,
                ActiveLimit: > 0,
                PendingLimit: > 0
            }
            || descriptor.Capacity.Active > descriptor.Capacity.ActiveLimit
            || descriptor.Capacity.Pending > descriptor.Capacity.PendingLimit)
            throw new JsonException("MeshNode descriptor capacity is invalid.");
        if (descriptor.ObjectCapabilities is null
            || descriptor.ObjectCapabilities.Count > 1024)
            throw new JsonException(
                "MeshNode descriptor has more than 1024 object capabilities.");
        if (descriptor.ObjectRole != ZLinkMeshNodeObjectRole.Server
            && descriptor.ObjectCapabilities.Count != 0)
            throw new JsonException(
                "Only an Object Server descriptor can publish object capabilities.");

        var identities = new HashSet<(ZLinkPlacementObjectKind, string)>();
        foreach (var capability in descriptor.ObjectCapabilities)
        {
            if (capability is null
                || capability.PlacementProfiles is null
                || !Enum.IsDefined(capability.ObjectKind)
                || !Enum.IsDefined(capability.Policy)
                || !identities.Add((
                    capability.ObjectKind,
                    capability.StableType)))
                throw new JsonException(
                    "MeshNode descriptor object capabilities are invalid or duplicated.");
            RequireUtf8Value(
                capability.StableType,
                nameof(capability.StableType));
            if ((capability.Policy
                 == ZLinkObjectMaintenancePolicyKind.Snapshot)
                != capability.HasSnapshotAdapter)
                throw new JsonException(
                    "Snapshot capability and adapter presence are inconsistent.");
            if (capability.ActiveLimit is <= 0
                || capability.PendingLimit is <= 0
                || capability.PlacementProfiles.Count > 1024)
                throw new JsonException(
                    "Object capability limits or placement profiles are invalid.");
            foreach (var profile in capability.PlacementProfiles)
                RequireUtf8Value(profile, "PlacementProfile");
        }
    }

    private static void RequireUtf8Value(string value, string name)
    {
        var size = System.Text.Encoding.UTF8.GetByteCount(value);
        if (size is < 1 or > 255 || value.Contains('\0'))
            throw new JsonException(
                $"{name} must be 1 to 255 UTF-8 bytes without NUL.");
    }

    /// <summary>Canonical descriptor JSON orders ChannelWeights properties
    /// by ChannelName byte order and rejects duplicates (spec 41 §2).</summary>
    private sealed class ChannelWeightsJsonConverter : JsonConverter<IReadOnlyDictionary<string, int>>
    {
        public override IReadOnlyDictionary<string, int> Read(
            ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
                throw new JsonException("ChannelWeights must be a JSON object.");
            var weights = new Dictionary<string, int>(StringComparer.Ordinal);
            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.EndObject) return weights;
                var name = reader.GetString()
                           ?? throw new JsonException("ChannelWeights property name expected.");
                reader.Read();
                if (!weights.TryAdd(name, reader.GetInt32()))
                    throw new JsonException($"Duplicate ChannelWeights entry '{name}'.");
            }

            throw new JsonException("ChannelWeights object was not closed.");
        }

        public override void Write(
            Utf8JsonWriter writer, IReadOnlyDictionary<string, int> value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            foreach (var (name, weight) in value.OrderBy(
                         static pair => pair.Key, Utf8StringComparer.Instance))
                writer.WriteNumber(name, weight);
            writer.WriteEndObject();
        }
    }

    private sealed class StringSetJsonConverter : JsonConverter<IReadOnlySet<string>>
    {
        public override IReadOnlySet<string> Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartArray)
                throw new JsonException("String set must be a JSON array.");
            var values = new HashSet<string>(StringComparer.Ordinal);
            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.EndArray)
                    return values;
                if (reader.TokenType != JsonTokenType.String)
                    throw new JsonException("String set entries must be strings.");
                var value = reader.GetString()
                            ?? throw new JsonException(
                                "String set entries must not be null.");
                if (!values.Add(value))
                    throw new JsonException(
                        $"Duplicate string set entry '{value}'.");
            }
            throw new JsonException("String set array was not closed.");
        }

        public override void Write(
            Utf8JsonWriter writer,
            IReadOnlySet<string> value,
            JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            foreach (var entry in value.Order(Utf8StringComparer.Instance))
                writer.WriteStringValue(entry);
            writer.WriteEndArray();
        }
    }

    private sealed class ObjectCapabilitiesJsonConverter
        : JsonConverter<IReadOnlyList<ZLinkObjectCapability>>
    {
        public override IReadOnlyList<ZLinkObjectCapability> Read(
            ref Utf8JsonReader reader,
            Type typeToConvert,
            JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartArray)
                throw new JsonException(
                    "ObjectCapabilities must be a JSON array.");
            var values = new List<ZLinkObjectCapability>();
            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.EndArray)
                    return values;
                var value = JsonSerializer.Deserialize<ZLinkObjectCapability>(
                                ref reader,
                                options)
                            ?? throw new JsonException(
                                "Object capability must not be null.");
                values.Add(value);
            }
            throw new JsonException(
                "ObjectCapabilities array was not closed.");
        }

        public override void Write(
            Utf8JsonWriter writer,
            IReadOnlyList<ZLinkObjectCapability> value,
            JsonSerializerOptions options)
        {
            writer.WriteStartArray();
            foreach (var capability in value
                         .OrderBy(static item => item.ObjectKind)
                         .ThenBy(
                             static item => item.StableType,
                             Utf8StringComparer.Instance))
                JsonSerializer.Serialize(writer, capability, options);
            writer.WriteEndArray();
        }
    }

    private sealed class Utf8StringComparer : IComparer<string>
    {
        internal static Utf8StringComparer Instance { get; } = new();

        public int Compare(string? left, string? right)
        {
            if (ReferenceEquals(left, right)) return 0;
            if (left is null) return -1;
            if (right is null) return 1;
            return System.Text.Encoding.UTF8.GetBytes(left)
                .AsSpan()
                .SequenceCompareTo(
                    System.Text.Encoding.UTF8.GetBytes(right));
        }
    }

    /// <summary>Routing ids are opaque bytes (1..255); hex is the only
    /// lossless text form. Nullable routing ids serialize as JSON null via
    /// the standard nullable handling.</summary>
    private sealed class RoutingIdJsonConverter : JsonConverter<RoutingId>
    {
        public override RoutingId Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            var hex = reader.GetString();
            return string.IsNullOrEmpty(hex) ? default : RoutingId.FromHex(hex);
        }

        public override void Write(Utf8JsonWriter writer, RoutingId value, JsonSerializerOptions options) =>
            writer.WriteStringValue(value.IsEmpty ? string.Empty : value.ToHex());
    }

    /// <summary>Actor ref storage is a Redis codec detail. This is a
    /// cross-language row format change from the former ad hoc string value
    /// to typed fields that every backend can read without knowing a
    /// runtime-side string format.</summary>
    private sealed class ActorRefJsonConverter : JsonConverter<ActorRef>
    {
        public override ActorRef Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
        {
            if (reader.TokenType != JsonTokenType.StartObject)
            {
                throw new JsonException("ActorRef must be a JSON object.");
            }

            RoutingId nodeRid = default;
            string? actorId = null;
            ulong generation = 0;
            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.EndObject)
                {
                    return new ActorRef(
                        nodeRid,
                        actorId ?? throw new JsonException("ActorRef.actorId is required."),
                        generation);
                }

                if (reader.TokenType != JsonTokenType.PropertyName)
                {
                    throw new JsonException("ActorRef property name expected.");
                }

                var propertyName = reader.GetString();
                reader.Read();
                switch (propertyName)
                {
                    case "nodeRid":
                    case "NodeRid":
                        nodeRid = ReadRoutingId(ref reader);
                        break;

                    case "actorId":
                    case "ActorId":
                        actorId = reader.GetString();
                        break;

                    case "generation":
                    case "Generation":
                        generation = reader.GetUInt64();
                        break;

                    default:
                        reader.Skip();
                        break;
                }
            }

            throw new JsonException("ActorRef object was not closed.");
        }

        public override void Write(Utf8JsonWriter writer, ActorRef value, JsonSerializerOptions options)
        {
            writer.WriteStartObject();
            writer.WriteString("nodeRid", value.NodeRid.IsEmpty ? string.Empty : value.NodeRid.ToHex());
            writer.WriteString("actorId", value.ActorId);
            writer.WriteNumber("generation", value.Generation);
            writer.WriteEndObject();
        }

        private static RoutingId ReadRoutingId(ref Utf8JsonReader reader)
        {
            var hex = reader.GetString();
            return string.IsNullOrEmpty(hex) ? default : RoutingId.FromHex(hex);
        }
    }
}
