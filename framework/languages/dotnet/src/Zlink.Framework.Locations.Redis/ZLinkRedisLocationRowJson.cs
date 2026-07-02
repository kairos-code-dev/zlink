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
        Converters = { new RoutingIdJsonConverter() }
    };

    internal static string Serialize<TRow>(TRow row) =>
        JsonSerializer.Serialize(row, Options);

    internal static TRow Deserialize<TRow>(string json) =>
        JsonSerializer.Deserialize<TRow>(json, Options)
        ?? throw new InvalidOperationException("Location row payload deserialized to null.");

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
}
