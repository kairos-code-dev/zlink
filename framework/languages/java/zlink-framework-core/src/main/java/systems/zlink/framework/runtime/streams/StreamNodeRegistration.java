package systems.zlink.framework.runtime.streams;

import java.util.List;
import java.util.ArrayList;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.mesh.MeshNodeRegistration;
import systems.zlink.framework.streams.ZLinkSession;

public final class StreamNodeRegistration {
    private final String name;
    private final List<String> bindEndpoints = new ArrayList<>();
    private String advertiseHost;
    private TlsServerRegistration tlsServer;
    private Class<? extends ZLinkSession> sessionType;
    private String actorDispatchMeshName;
    private final List<Class<?>> sessionPacketHandlers =
        new ArrayList<>();

    public StreamNodeRegistration(String name) {
        this.name = name;
    }

    public String name() {
        return name;
    }

    public String bindEndpoint() {
        return bindEndpoints.isEmpty() ? null : bindEndpoints.get(0);
    }

    public List<String> bindEndpoints() {
        return List.copyOf(bindEndpoints);
    }

    public Class<? extends ZLinkSession> sessionType() {
        return sessionType;
    }

    public TlsServerRegistration tlsServer() {
        return tlsServer;
    }

    public List<Class<?>> sessionPacketHandlers() {
        return List.copyOf(sessionPacketHandlers);
    }

    public String actorDispatchMeshName() {
        return actorDispatchMeshName;
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
        bindEndpoints.add(endpoint);
    }

    void replaceBind(String endpoint) {
        bindEndpoints.clear();
        bind(endpoint);
    }

    void setAdvertiseHost(String host) {
        advertiseHost = host;
    }

    public String advertisedEndpoint(String actualEndpoint) {
        if (advertiseHost == null || actualEndpoint == null
            || !actualEndpoint.startsWith("tcp://")) {
            return actualEndpoint;
        }
        java.net.URI value = java.net.URI.create(actualEndpoint);
        try {
            return new java.net.URI(
                value.getScheme(), value.getUserInfo(), advertiseHost,
                value.getPort(), value.getPath(), value.getQuery(), value.getFragment())
                .toString();
        } catch (java.net.URISyntaxException invalid) {
            throw new ZLinkConfigurationException(
                "invalid stream advertise host: " + advertiseHost);
        }
    }

    void setTlsServer(
        String certificatePath,
        String keyPath,
        boolean requireClientCertificate) {
        if (certificatePath == null || certificatePath.isBlank()) {
            throw new ZLinkConfigurationException("stream TLS certificate path is required: " + name);
        }
        if (keyPath == null || keyPath.isBlank()) {
            throw new ZLinkConfigurationException("stream TLS key path is required: " + name);
        }
        tlsServer = new TlsServerRegistration(
            certificatePath,
            keyPath,
            requireClientCertificate);
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

    void enableActorDispatch(String meshName) {
        if (meshName == null || meshName.isBlank()) {
            throw new ZLinkConfigurationException("actor dispatch MeshName is required: " + name);
        }
        actorDispatchMeshName = meshName;
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

    public void validate(List<MeshNodeRegistration> meshNodes) {
        if (bindEndpoints.isEmpty()) {
            throw new ZLinkConfigurationException("stream node bind endpoint is required: " + name);
        }
        if (sessionType == null) {
            throw new ZLinkConfigurationException("stream node session type is required: " + name);
        }
        if (actorDispatchMeshName != null
            && meshNodes.stream().noneMatch(
                mesh -> mesh.meshName().equals(actorDispatchMeshName))) {
            throw new ZLinkConfigurationException(
                "stream actor dispatch RouteMesh is not configured: "
                    + actorDispatchMeshName);
        }
    }

    public record TlsServerRegistration(
        String certificatePath,
        String keyPath,
        boolean requireClientCertificate) {
    }
}
