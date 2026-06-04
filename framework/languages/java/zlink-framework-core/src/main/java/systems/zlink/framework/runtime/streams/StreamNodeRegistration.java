package systems.zlink.framework.runtime.streams;

import java.util.List;
import java.util.ArrayList;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;
import systems.zlink.framework.streams.ZLinkSession;
import systems.zlink.framework.streams.ZLinkSessionPacketHandler;

public final class StreamNodeRegistration {
    private final String name;
    private String bindEndpoint;
    private String actorGatewaySpotNodeName;
    private Class<? extends ZLinkSession> sessionType;
    private final List<Class<? extends ZLinkSessionPacketHandler<?>>> sessionPacketHandlers =
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

    public String actorGatewaySpotNodeName() {
        return actorGatewaySpotNodeName;
    }

    public Class<? extends ZLinkSession> sessionType() {
        return sessionType;
    }

    public List<Class<? extends ZLinkSessionPacketHandler<?>>> sessionPacketHandlers() {
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

    void attachActorGateway(String spotNodeName) {
        if (spotNodeName == null || spotNodeName.isBlank()) {
            throw new ZLinkConfigurationException("actor gateway SpotNode name is required: " + name);
        }
        actorGatewaySpotNodeName = spotNodeName;
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

    void addSessionPacketHandler(
        Class<? extends ZLinkSessionPacketHandler<?>> handlerType) {
        if (handlerType == null) {
            throw new ZLinkConfigurationException(
                "session packet handler type is required: " + name);
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
        if (actorGatewaySpotNodeName != null) {
            SpotNodeRegistration target = spotNodes.stream()
                .filter(node -> actorGatewaySpotNodeName.equals(node.nodeName()))
                .findFirst()
                .orElse(null);
            if (target == null) {
                throw new ZLinkConfigurationException(
                    "stream node actor gateway SpotNode is not configured: "
                        + actorGatewaySpotNodeName);
            }
            if (!target.routerEnabled()) {
                throw new ZLinkConfigurationException(
                    "stream node actor gateway SpotNode does not enable router capability: "
                        + actorGatewaySpotNodeName);
            }
        }
    }
}
