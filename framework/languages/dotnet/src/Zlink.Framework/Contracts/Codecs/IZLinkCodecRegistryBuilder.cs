namespace Zlink.Framework.Contracts.Codecs;

public interface IZLinkCodecRegistryBuilder
{
    void AddProtobuf();

    void AddJson();

    void AddMessagePack();

    /// <summary>
    /// Registers a custom payload serializer under a content type (for example
    /// <c>"application/avro"</c>). The registered serializer becomes the payload
    /// codec for high-level object messaging. At most one custom serializer may be
    /// registered; registering a second one is a configuration error.
    /// </summary>
    void AddSerializer(string contentType, IZLinkMessageSerializer serializer);
}
