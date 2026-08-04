package systems.zlink.framework.runtime.internal.configuration;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.function.Predicate;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder;
import systems.zlink.framework.configuration.ZLinkCodecRegistrar;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.errors.ZLinkFrameworkException;
import systems.zlink.framework.errors.ZLinkFrameworkErrorKind;
import systems.zlink.framework.streams.ZLinkStreamCodec;

public final class ZLinkCodecRegistration implements ZLinkCodecRegistryBuilder, ZLinkCodecRegistrar {
    private static final String DEFAULT_JSON_CONTENT_TYPE = "application/json";
    private static final String LEGACY_JSON_CONTENT_TYPE =
        "application/zlink-framework-json-v1";
    private final Map<String, RegisteredSerializer> serializers = new LinkedHashMap<>();
    private final Map<String, ZLinkStreamCodec> streamCodecsByContentType = new LinkedHashMap<>();
    private final Map<ZLinkStreamCodec, String> contentTypesByStreamCodec = new LinkedHashMap<>();

    @Override
    public void use(systems.zlink.framework.configuration.ZLinkCodecExtension extension) {
        Objects.requireNonNull(extension, "extension").register(this);
    }

    @Override
    public void addSerializer(String contentType, ZLinkMessageSerializer serializer) {
        addSerializer(contentType, serializer, ignored -> true, true);
    }

    @Override
    public void addSerializer(
        String contentType,
        ZLinkMessageSerializer serializer,
        Predicate<Class<?>> canSerialize) {
        addSerializer(contentType, serializer, canSerialize, false);
    }

    private void addSerializer(
        String contentType,
        ZLinkMessageSerializer serializer,
        Predicate<Class<?>> canSerialize,
        boolean fallbackSerializer) {
        Objects.requireNonNull(contentType, "contentType");
        Objects.requireNonNull(serializer, "serializer");
        Objects.requireNonNull(canSerialize, "canSerialize");
        String normalized = contentType.trim();
        if (normalized.isEmpty()) {
            throw new ZLinkConfigurationException("custom serializer content type must not be blank");
        }
        serializers.put(normalized, new RegisteredSerializer(serializer, canSerialize, fallbackSerializer));
    }

    @Override
    public void addStreamCodec(String contentType, ZLinkStreamCodec codec) {
        Objects.requireNonNull(contentType, "contentType");
        Objects.requireNonNull(codec, "codec");
        String normalized = contentType.trim();
        if (normalized.isEmpty()) {
            throw new ZLinkConfigurationException("stream codec content type must not be blank");
        }
        streamCodecsByContentType.put(normalized, codec);
        contentTypesByStreamCodec.put(codec, normalized);
    }

    public Map<String, ZLinkMessageSerializer> serializers() {
        Map<String, ZLinkMessageSerializer> snapshot = new LinkedHashMap<>();
        serializers.forEach((contentType, serializer) -> snapshot.put(contentType, serializer.serializer()));
        return Collections.unmodifiableMap(snapshot);
    }

    public Optional<ZLinkStreamCodec> streamCodec(String contentType) {
        if (contentType == null) {
            return Optional.empty();
        }
        return Optional.ofNullable(
            streamCodecsByContentType.get(contentType.trim()));
    }

    /**
     * Resolves the stream marker carried by an incoming application envelope.
     * JSON is the only built-in content type; every other type must have an
     * explicit immutable registration.
     */
    public Optional<ZLinkStreamCodec> streamCodecForReceivedContentType(
        String contentType) {
        if (contentType == null) {
            return Optional.empty();
        }
        String normalized = contentType.trim();
        if (DEFAULT_JSON_CONTENT_TYPE.equalsIgnoreCase(normalized)
            || LEGACY_JSON_CONTENT_TYPE.equalsIgnoreCase(normalized)) {
            return Optional.of(ZLinkStreamCodec.JSON);
        }
        return streamCodec(normalized);
    }

    public Optional<String> streamContentType(ZLinkStreamCodec codec) {
        return Optional.ofNullable(contentTypesByStreamCodec.get(codec));
    }

    public Optional<ZLinkStreamCodec> streamCodecForCustomSerializer() {
        Optional<Map.Entry<String, RegisteredSerializer>> fallbackSerializer =
            singleFallbackSerializer();
        if (fallbackSerializer.isEmpty()) {
            if (serializers.size() == 1) {
                return streamCodec(serializers.keySet().iterator().next());
            }
            return Optional.empty();
        }
        return streamCodec(fallbackSerializer.get().getKey());
    }

    /**
     * Returns the single registered custom serializer, if any. Throws when more
     * than one custom serializer is registered because the payload serializer is
     * then ambiguous.
     */
    public Optional<ZLinkMessageSerializer> customSerializer() {
        return singleFallbackSerializer()
            .map(entry -> entry.getValue().serializer());
    }

    private Optional<Map.Entry<String, RegisteredSerializer>> singleFallbackSerializer() {
        Map<String, RegisteredSerializer> fallbackSerializers = new LinkedHashMap<>();
        serializers.forEach((contentType, serializer) -> {
            if (serializer.fallbackSerializer()) {
                fallbackSerializers.put(contentType, serializer);
            }
        });

        if (fallbackSerializers.isEmpty()) {
            return Optional.empty();
        }
        if (fallbackSerializers.size() > 1) {
            throw new ZLinkConfigurationException(
                "payload serializer is ambiguous because more than one custom serializer is registered: "
                    + fallbackSerializers.keySet());
        }
        return Optional.of(fallbackSerializers.entrySet().iterator().next());
    }

    public ZLinkMessageSerializer serializerWithFallback(ZLinkMessageSerializer fallback) {
        Objects.requireNonNull(fallback, "fallback");
        if (serializers.isEmpty()) {
            return fallback;
        }
        return new CompositeSerializer(serializers, fallback);
    }

    public String contentTypeFor(Class<?> type) {
        if (type == null) {
            return DEFAULT_JSON_CONTENT_TYPE;
        }
        return singleSerializerFor(serializers, type)
            .map(Map.Entry::getKey)
            .orElse(DEFAULT_JSON_CONTENT_TYPE);
    }

    /**
     * Resolves the serializer selected by an incoming wire content type.
     * Incoming non-JSON content types are strict: the JSON fallback is not
     * allowed to reinterpret a payload whose envelope selected another type.
     */
    public ZLinkMessageSerializer serializerForReceivedContentType(
        String contentType,
        ZLinkMessageSerializer jsonFallback) {
        Objects.requireNonNull(contentType, "contentType");
        Objects.requireNonNull(jsonFallback, "jsonFallback");
        String normalized = contentType.trim();
        if (normalized.isEmpty()) {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PROTOCOL_ERROR,
                "received payload content type must not be blank");
        }
        if (DEFAULT_JSON_CONTENT_TYPE.equalsIgnoreCase(normalized)
            || LEGACY_JSON_CONTENT_TYPE.equalsIgnoreCase(normalized)) {
            return jsonFallback;
        }
        RegisteredSerializer registered = serializers.get(normalized);
        if (registered == null) {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.PROTOCOL_ERROR,
                "No payload serializer is registered for received content type '"
                    + normalized + "'");
        }
        return registered.serializer();
    }

    private static Optional<Map.Entry<String, RegisteredSerializer>> singleSerializerFor(
        Map<String, RegisteredSerializer> serializers,
        Class<?> type) {
        var matches = serializers.entrySet().stream()
            .filter(entry -> entry.getValue().canSerialize().test(type))
            .toList();
        if (matches.isEmpty()) {
            return Optional.empty();
        }
        if (matches.size() > 1) {
            throw new ZLinkConfigurationException(
                "payload serializer is ambiguous for type " + type.getName() + ": "
                    + matches.stream().map(Map.Entry::getKey).toList());
        }
        return Optional.of(matches.get(0));
    }

    private record RegisteredSerializer(
        ZLinkMessageSerializer serializer,
        Predicate<Class<?>> canSerialize,
        boolean fallbackSerializer) {
    }

    private static final class CompositeSerializer implements ZLinkMessageSerializer {
        private final Map<String, RegisteredSerializer> serializers;
        private final ZLinkMessageSerializer fallback;

        CompositeSerializer(
            Map<String, RegisteredSerializer> serializers,
            ZLinkMessageSerializer fallback) {
            this.serializers = Map.copyOf(serializers);
            this.fallback = fallback;
        }

        @Override
        public <T> systems.zlink.framework.ZLinkEncodedPayload serialize(T value) {
            if (value != null) {
                return serializerFor(value.getClass()).serialize(value);
            }
            return fallback.serialize(value);
        }

        @Override
        public <T> T deserialize(systems.zlink.framework.ZLinkEncodedPayload payload, Class<T> type) {
            return serializerFor(type).deserialize(payload, type);
        }

        @Override
        public void prepare(Class<?> type) {
            serializerFor(type).prepare(type);
        }

        private ZLinkMessageSerializer serializerFor(Class<?> type) {
            return singleSerializerFor(serializers, type)
                .map(entry -> entry.getValue().serializer())
                .orElse(fallback);
        }
    }
}
