namespace Zlink.Framework.Contracts.Codecs;

/// <summary>
/// Custom payload serializer for high-level object messaging. Convert a business
/// object to and from a wire <see cref="Message"/>. Register an implementation
/// with <see cref="IZLinkCodecRegistryBuilder.AddSerializer"/> under a content
/// type (for example <c>"application/avro"</c>) and the framework uses it to
/// encode and decode channel payloads.
/// </summary>
public interface IZLinkMessageSerializer
{
    /// <summary>Serializes <paramref name="value"/> (declared as <paramref name="type"/>) to a wire message.</summary>
    Message Serialize(object value, Type type);

    /// <summary>Deserializes <paramref name="message"/> back into an instance of <paramref name="type"/>.</summary>
    object? Deserialize(Message message, Type type);
}
