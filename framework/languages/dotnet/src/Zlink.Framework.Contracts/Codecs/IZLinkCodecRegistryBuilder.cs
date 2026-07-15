namespace Zlink.Framework.Contracts.Codecs;

public interface IZLinkCodecRegistryBuilder
{
    void Use(IZLinkCodecExtension extension);
}

public interface IZLinkCodecRegistrar
{
    /// <summary>
    ///     Registers a custom payload serializer under a content type (for example
    ///     <c>"application/avro"</c>). The registered serializer becomes the payload
    ///     codec for high-level object messaging. At most one custom serializer may be
    ///     registered; registering a second one is a configuration error.
    /// </summary>
    void AddSerializer(string contentType, IZLinkMessageSerializer serializer);

    /// <summary>
    ///     Registers a payload serializer that is used only when
    ///     <paramref name="canSerialize" /> returns <c>true</c> for the declared
    ///     payload type.
    /// </summary>
    void AddSerializer(
        string contentType,
        IZLinkMessageSerializer serializer,
        Func<Type, bool> canSerialize);

    /// <summary>
    ///     Maps a registered serializer content type to the stream packet codec value
    ///     used on the wire. Framework codec extensions call this when the same codec
    ///     must be shared by framework actors, stream connectors, and HTTP clients.
    /// </summary>
    void AddStreamCodec(string contentType, ZlinkStreamCodec codec);
}
