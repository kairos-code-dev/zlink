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
        Converters = { new RoutingIdJsonConverter(), new ActorRefJsonConverter() }
    };

    internal static string Serialize<TRow>(TRow row) =>
        JsonSerializer.Serialize(row, Options);

    internal static TRow Deserialize<TRow>(string json)
    {
        if (typeof(TRow) == typeof(ZLinkPeerLocation))
            RequirePeerDrainingField(json);

        return JsonSerializer.Deserialize<TRow>(json, Options)
               ?? throw new InvalidOperationException("Location row payload deserialized to null.");
    }

    private static void RequirePeerDrainingField(string json)
    {
        using var document = JsonDocument.Parse(json);
        if (document.RootElement.ValueKind != JsonValueKind.Object
            || !document.RootElement.EnumerateObject().Any(
                property => property.Name.Equals("Draining", StringComparison.OrdinalIgnoreCase)))
        {
            throw new JsonException("Peer location row must contain the typed Draining field.");
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
