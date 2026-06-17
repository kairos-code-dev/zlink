package systems.zlink.framework.configuration;

import systems.zlink.framework.ZLinkMessageSerializer;

public interface ZLinkCodecRegistryBuilder {
    void addJson();

    void addMessagePack();

    void addProtobuf();

    /**
     * Registers a custom payload serializer under a content type (for example
     * {@code "application/avro"}). The registered serializer becomes the payload
     * codec for high-level object messaging. At most one custom serializer may be
     * registered; registering a second one is a configuration error.
     */
    void addSerializer(String contentType, ZLinkMessageSerializer serializer);
}
