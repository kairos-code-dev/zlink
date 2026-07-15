using System.Text.Json;
using System.Text.Json.Serialization;

namespace Zlink.Framework.Runtime.Codecs;

internal sealed class ActorRefSnapshotJsonConverter : JsonConverter<ActorRefSnapshot>
{
    public override ActorRefSnapshot Read(
        ref Utf8JsonReader reader,
        Type typeToConvert,
        JsonSerializerOptions options)
    {
        using var document = JsonDocument.ParseValue(ref reader);
        var root = document.RootElement;
        var nodeRid = root.GetProperty("nodeRid").GetString();
        return new ActorRefSnapshot(
            string.IsNullOrEmpty(nodeRid) ? default : RoutingId.FromHex(nodeRid),
            root.GetProperty("actorId").GetString() ?? string.Empty,
            root.GetProperty("generation").GetUInt64());
    }

    public override void Write(
        Utf8JsonWriter writer,
        ActorRefSnapshot value,
        JsonSerializerOptions options)
    {
        writer.WriteStartObject();
        writer.WriteString("nodeRid", value.NodeRid.IsEmpty ? string.Empty : value.NodeRid.ToHex());
        writer.WriteString("actorId", value.ActorId);
        writer.WriteNumber("generation", value.Generation);
        writer.WriteEndObject();
    }
}
