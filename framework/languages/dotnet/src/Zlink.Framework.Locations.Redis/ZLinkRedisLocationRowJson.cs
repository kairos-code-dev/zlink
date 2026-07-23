using System.Text.Json;
using System.Text.Json.Serialization;

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
            new StringSetJsonConverter()
        }
    };

    internal static string Serialize<TRow>(TRow row) =>
        JsonSerializer.Serialize(row, Options);

    internal static TRow Deserialize<TRow>(string json)
    {
        if (typeof(TRow) == typeof(ZLinkMeshNodeDescriptor))
            RequireDescriptorDrainingField(json);

        return JsonSerializer.Deserialize<TRow>(json, Options)
               ?? throw new InvalidOperationException("Location row payload deserialized to null.");
    }

    private static void RequireDescriptorDrainingField(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object
            || !document.RootElement.EnumerateObject().Any(
                property => property.Name.Equals("Draining", StringComparison.OrdinalIgnoreCase)))
        {
            throw new JsonException("MeshNode descriptor row must contain the typed Draining field.");
        }
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
                         static pair => pair.Key, StringComparer.Ordinal))
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
            foreach (var entry in value.Order(StringComparer.Ordinal))
                writer.WriteStringValue(entry);
            writer.WriteEndArray();
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
