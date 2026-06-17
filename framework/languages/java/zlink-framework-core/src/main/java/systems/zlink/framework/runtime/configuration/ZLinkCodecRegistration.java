package systems.zlink.framework.runtime.configuration;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import systems.zlink.framework.ZLinkMessageSerializer;
import systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder;
import systems.zlink.framework.errors.ZLinkConfigurationException;

public final class ZLinkCodecRegistration implements ZLinkCodecRegistryBuilder {
    private final Set<String> registeredCodecs = new LinkedHashSet<>();
    private final Map<String, ZLinkMessageSerializer> serializers = new LinkedHashMap<>();

    @Override
    public void addJson() {
        registeredCodecs.add("json");
    }

    @Override
    public void addMessagePack() {
        registeredCodecs.add("messagepack");
    }

    @Override
    public void addProtobuf() {
        registeredCodecs.add("protobuf");
    }

    @Override
    public void addSerializer(String contentType, ZLinkMessageSerializer serializer) {
        Objects.requireNonNull(contentType, "contentType");
        Objects.requireNonNull(serializer, "serializer");
        String normalized = contentType.trim();
        if (normalized.isEmpty()) {
            throw new ZLinkConfigurationException("custom serializer content type must not be blank");
        }
        serializers.put(normalized, serializer);
    }

    public Set<String> registeredCodecs() {
        return Collections.unmodifiableSet(registeredCodecs);
    }

    public Map<String, ZLinkMessageSerializer> serializers() {
        return Collections.unmodifiableMap(serializers);
    }

    /**
     * Returns the single registered custom serializer, if any. Throws when more
     * than one custom serializer is registered because the payload serializer is
     * then ambiguous.
     */
    public Optional<ZLinkMessageSerializer> customSerializer() {
        if (serializers.isEmpty()) {
            return Optional.empty();
        }
        if (serializers.size() > 1) {
            throw new ZLinkConfigurationException(
                "payload serializer is ambiguous because more than one custom serializer is registered: "
                    + serializers.keySet());
        }
        return Optional.of(serializers.values().iterator().next());
    }
}
