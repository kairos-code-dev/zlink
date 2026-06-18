package systems.zlink.framework.configuration;

import java.util.function.Predicate;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.streams.ZLinkStreamCodec;

public interface ZLinkCodecRegistryBuilder {
    default void use(ZLinkCodecExtension extension) {
        extension.register(this);
    }

    void addJson();

    /**
     * Registers a custom payload serializer under a content type (for example
     * {@code "application/avro"}). The registered serializer becomes the payload
     * codec for high-level object messaging. At most one custom serializer may be
     * registered; registering a second one is a configuration error.
     */
    void addSerializer(String contentType, ZLinkMessageSerializer serializer);

    /**
     * Registers a payload serializer that is used only when {@code canSerialize}
     * returns {@code true} for the declared payload type.
     */
    void addSerializer(
        String contentType,
        ZLinkMessageSerializer serializer,
        Predicate<Class<?>> canSerialize);

    /**
     * Maps a registered serializer content type to the stream packet codec value
     * used on the wire. Framework codec extensions call this when framework
     * actors, stream connectors, and HTTP clients share the same codec package.
     */
    void addStreamCodec(String contentType, ZLinkStreamCodec codec);
}
