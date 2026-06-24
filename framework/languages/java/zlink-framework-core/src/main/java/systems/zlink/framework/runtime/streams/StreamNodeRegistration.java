package systems.zlink.framework.runtime.streams;

import java.util.List;
import java.util.ArrayList;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;
import systems.zlink.framework.streams.ZLinkSession;

public final class StreamNodeRegistration {
    private final String name;
    private String bindEndpoint;
    private Class<? extends ZLinkSession> sessionType;
    private final List<Class<?>> sessionPacketHandlers =
        new ArrayList<>();

    public StreamNodeRegistration(String name) {
        this.name = name;
    }

    public String name() {
        return name;
    }

    public String bindEndpoint() {
        return bindEndpoint;
    }

    public Class<? extends ZLinkSession> sessionType() {
        return sessionType;
    }

    public List<Class<?>> sessionPacketHandlers() {
        return List.copyOf(sessionPacketHandlers);
    }

    public List<Class<?>> applicationTypes() {
        List<Class<?>> types = new ArrayList<>();
        if (sessionType != null) {
            types.add(sessionType);
        }
        types.addAll(sessionPacketHandlers);
        return List.copyOf(types);
    }

    void bind(String endpoint) {
        if (endpoint == null || endpoint.isBlank()) {
            throw new ZLinkConfigurationException("stream bind endpoint is required: " + name);
        }
        bindEndpoint = endpoint;
    }

    void registerSession(Class<? extends ZLinkSession> type) {
        if (type == null) {
            throw new ZLinkConfigurationException("session type is required: " + name);
        }
        if (sessionType != null) {
            throw new ZLinkConfigurationException(
                "stream node registers multiple sessions: " + name);
        }
        sessionType = type;
    }

    public void addSessionPacketHandler(Class<?> handlerType) {
        if (handlerType == null) {
            throw new ZLinkConfigurationException(
                "session packet handler type is required: " + name);
        }
        if (sessionPacketHandlers.contains(handlerType)) {
            return;
        }
        sessionPacketHandlers.add(handlerType);
    }

    public void validate(List<SpotNodeRegistration> spotNodes) {
        if (bindEndpoint == null) {
            throw new ZLinkConfigurationException("stream node bind endpoint is required: " + name);
        }
        if (sessionType == null) {
            throw new ZLinkConfigurationException("stream node session type is required: " + name);
        }
    }
}
