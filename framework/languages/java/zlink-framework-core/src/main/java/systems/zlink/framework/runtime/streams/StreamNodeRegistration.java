package systems.zlink.framework.runtime.streams;

import java.util.List;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;
import systems.zlink.framework.streams.ZLinkSession;

public final class StreamNodeRegistration {
    private final String name;
    private String bindEndpoint;
    private String actorGatewaySpotNodeName;
    private Class<? extends ZLinkSession> sessionType;

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

    public void validate(List<SpotNodeRegistration> spotNodes) {
        if (bindEndpoint == null) {
            throw new ZLinkConfigurationException("stream node bind endpoint is required: " + name);
        }
        if (sessionType == null) {
            throw new ZLinkConfigurationException("stream node session type is required: " + name);
        }
        if (actorGatewaySpotNodeName != null) {
            boolean found = spotNodes.stream()
                .anyMatch(node -> actorGatewaySpotNodeName.equals(node.nodeName()));
            if (!found) {
                throw new ZLinkConfigurationException(
                    "stream node actor gateway SpotNode is not configured: "
                        + actorGatewaySpotNodeName);
            }
        }
    }
}
