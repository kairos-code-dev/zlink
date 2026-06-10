package systems.zlink.framework.runtime.configuration;

import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.channels.ChannelRegistration;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.spots.SpotChannelClientRegistration;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;
import systems.zlink.framework.runtime.spots.SpotRouteChannelAcceptanceRegistration;
import systems.zlink.framework.runtime.streams.StreamNodeRegistration;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class ZLinkFrameworkRegistration {
    private final ZLinkCodecRegistration codecs = new ZLinkCodecRegistration();
    private final ZLinkMetadataPolicyRegistration metadataPolicy =
        new ZLinkMetadataPolicyRegistration();
    private final ZLinkDispatchOptionsRegistration dispatchOptions =
        new ZLinkDispatchOptionsRegistration();
    private final List<String> registryEndpoints = new ArrayList<>();
    private final List<ChannelRegistration> channels = new ArrayList<>();
    private final List<SpotNodeRegistration> spotNodes = new ArrayList<>();
    private final List<StreamNodeRegistration> streamNodes = new ArrayList<>();
    private final Map<String, Class<? extends ZLinkActorFactory>> actorFactories =
        new LinkedHashMap<>();
    private final Set<Class<?>> handlerPackageMarkers = new LinkedHashSet<>();
    private final List<Class<? extends ZLinkHandlerFilter>> filters = new ArrayList<>();
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers = new ArrayList<>();
    private Executor handlerExecutor = Executors.newVirtualThreadPerTaskExecutor();
    private boolean closeHandlerExecutor = true;
    private Duration defaultTimeout = Duration.ofSeconds(30);
    private Class<? extends ZLinkSpotRemoteAddressResolver> spotRemoteAddressResolverType;
    private ZLinkRegistrySpotRemoteAddressesRegistration registrySpotRemoteAddresses;

    public Duration defaultTimeout() {
        return defaultTimeout;
    }

    void setDefaultTimeout(Duration defaultTimeout) {
        this.defaultTimeout = defaultTimeout;
    }

    public ZLinkCodecRegistration codecs() {
        return codecs;
    }

    public ZLinkMetadataPolicyRegistration metadataPolicy() {
        return metadataPolicy;
    }

    public ZLinkDispatchOptionsRegistration dispatchOptions() {
        return dispatchOptions;
    }

    public List<String> registryEndpoints() {
        return registryEndpoints;
    }

    public List<ChannelRegistration> channels() {
        return channels;
    }

    public List<SpotNodeRegistration> spotNodes() {
        return spotNodes;
    }

    public List<StreamNodeRegistration> streamNodes() {
        return streamNodes;
    }

    public Map<String, Class<? extends ZLinkActorFactory>> actorFactories() {
        return actorFactories;
    }

    public Set<Class<?>> handlerPackageMarkers() {
        return handlerPackageMarkers;
    }

    public List<Class<? extends ZLinkHandlerFilter>> filters() {
        return filters;
    }

    public List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers() {
        return List.copyOf(suspendHandlerInvokers);
    }

    public Executor handlerExecutor() {
        return handlerExecutor;
    }

    public boolean closeHandlerExecutor() {
        return closeHandlerExecutor;
    }

    public Class<? extends ZLinkSpotRemoteAddressResolver> spotRemoteAddressResolverType() {
        return spotRemoteAddressResolverType;
    }

    public Set<Class<?>> applicationTypes() {
        Set<Class<?>> types = new LinkedHashSet<>();
        types.addAll(filters);
        types.addAll(actorFactories.values());
        if (spotRemoteAddressResolverType != null) {
            types.add(spotRemoteAddressResolverType);
        }
        for (ChannelRegistration channel : channels) {
            types.addAll(channel.handlerTypes());
        }
        for (SpotNodeRegistration spotNode : spotNodes) {
            types.addAll(spotNode.spotFactories());
            types.addAll(spotNode.entrySpots());
        }
        for (StreamNodeRegistration streamNode : streamNodes) {
            types.addAll(streamNode.applicationTypes());
        }
        for (ZLinkScannedHandler handler : ZLinkHandlerScanner.scan(handlerPackageMarkers).handlers()) {
            types.add(handler.handlerType());
        }
        return Set.copyOf(types);
    }

    void setSpotRemoteAddressResolverType(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType) {
        spotRemoteAddressResolverType = resolverType;
    }

    public ZLinkRegistrySpotRemoteAddressesRegistration registrySpotRemoteAddresses() {
        return registrySpotRemoteAddresses;
    }

    void setRegistrySpotRemoteAddresses(
        ZLinkRegistrySpotRemoteAddressesRegistration registrySpotRemoteAddresses) {
        this.registrySpotRemoteAddresses = registrySpotRemoteAddresses;
    }

    void useVirtualThreadHandlers() {
        closeOwnedHandlerExecutor();
        handlerExecutor = Executors.newVirtualThreadPerTaskExecutor();
        closeHandlerExecutor = true;
    }

    void useHandlerExecutor(Executor executor) {
        if (executor == null) {
            throw new ZLinkConfigurationException("handler executor is required");
        }
        closeOwnedHandlerExecutor();
        handlerExecutor = executor;
        closeHandlerExecutor = false;
    }

    void useSuspendHandlerInvoker(ZLinkSuspendHandlerInvoker invoker) {
        if (invoker == null) {
            throw new ZLinkConfigurationException("suspend handler invoker is required");
        }
        suspendHandlerInvokers.clear();
        suspendHandlerInvokers.add(invoker);
    }

    public boolean discoveryEnabled() {
        return !registryEndpoints.isEmpty();
    }

    void validate() {
        dispatchOptions.validate();
        ZLinkScannedHandlerCatalog handlerCatalog =
            ZLinkHandlerScanner.scan(handlerPackageMarkers);
        Set<String> spotPublisherChannels = new LinkedHashSet<>();
        validateRegistrySpotRemoteAddresses();
        if (!actorFactories.isEmpty() && spotNodes.isEmpty()) {
            throw new ZLinkConfigurationException(
                "actor factories require at least one SpotNode");
        }
        for (ChannelRegistration channel : channels) {
            channel.validate(discoveryEnabled(), handlerCatalog);
        }
        for (SpotNodeRegistration spotNode : spotNodes) {
            spotNode.validate();
            for (SpotChannelClientRegistration attached :
                spotNode.attachedChannelClients().values()) {
                if (!discoveryEnabled() && attached.manualConnections().isEmpty()) {
                    throw new ZLinkConfigurationException(
                        "attached channel client requires discovery or manual connections: "
                            + attached.channelName());
                }
            }
            for (SpotRouteChannelAcceptanceRegistration acceptance :
                spotNode.acceptedSpotRouteChannels().values()) {
                validateAcceptedSpotRouteChannel(spotNode, acceptance);
            }
            for (String channelName : spotNode.attachedSpotPublisherClients().keySet()) {
                if (!spotPublisherChannels.add(channelName)) {
                    throw new ZLinkConfigurationException(
                        "duplicate SPOT publisher channel: " + channelName);
                }
            }
        }
        for (StreamNodeRegistration streamNode : streamNodes) {
            streamNode.validate(spotNodes);
        }
    }

    private void validateAcceptedSpotRouteChannel(
        SpotNodeRegistration spotNode,
        SpotRouteChannelAcceptanceRegistration acceptance) {
        if (!spotNode.routerEnabled()) {
            throw new ZLinkConfigurationException(
                "accepted SPOT route channel requires router capability: "
                    + spotNode.nodeName() + "/" + acceptance.channelName());
        }
        ChannelRegistration channel = findChannel(acceptance.channelName());
        if (channel == null) {
            throw new ZLinkConfigurationException(
                "accepted SPOT route channel is not registered: "
                    + acceptance.channelName());
        }
        if (channel.kind() != ChannelKind.CLIENT_SERVER
            && channel.kind() != ChannelKind.ROUTE_MESH) {
            throw new ZLinkConfigurationException(
                "accepted SPOT route channel must be client/server or route mesh: "
                    + acceptance.channelName());
        }
        if (!discoveryEnabled() && acceptance.manualConnections().isEmpty()) {
            throw new ZLinkConfigurationException(
                "accepted SPOT route channel requires discovery or manual connections: "
                    + acceptance.channelName());
        }
    }

    private ChannelRegistration findChannel(String channelName) {
        for (ChannelRegistration channel : channels) {
            if (channel.name().equals(channelName)) {
                return channel;
            }
        }
        return null;
    }

    private void validateRegistrySpotRemoteAddresses() {
        if (registrySpotRemoteAddresses == null) {
            return;
        }
        if (spotRemoteAddressResolverType != null) {
            throw new ZLinkConfigurationException(
                "registry SPOT remote addresses cannot be combined with a custom SPOT remote address resolver");
        }
        if (!discoveryEnabled()) {
            throw new ZLinkConfigurationException(
                "registry SPOT remote addresses require discovery endpoints");
        }
        long routeChannels = channels.stream()
            .filter(channel -> channel.kind() == ChannelKind.ROUTE_MESH)
            .count();
        if (routeChannels == 0) {
            throw new ZLinkConfigurationException(
                "registry SPOT remote addresses require AddRouteMeshChannel(...)");
        }
        if (spotNodes.isEmpty()) {
            return;
        }
        long spotDiscoverableNodes = spotNodes.stream()
            .filter(node -> node.routerEnabled() || node.pubSubEnabled())
            .count();
        if (spotDiscoverableNodes == 0) {
            throw new ZLinkConfigurationException(
                "registry SPOT remote addresses require at least one discoverable SpotNode");
        }
        if (registrySpotRemoteAddresses.routerChannelId() == null) {
            if (routeChannels != 1) {
                throw new ZLinkConfigurationException(
                    "registry SPOT remote addresses require routerChannelId when route mesh channel is ambiguous");
            }
            return;
        }
        ChannelRegistration routerChannel =
            findChannel(registrySpotRemoteAddresses.routerChannelId());
        if (routerChannel == null || routerChannel.kind() != ChannelKind.ROUTE_MESH) {
            throw new ZLinkConfigurationException(
                "registry SPOT remote addresses routerChannelId must name a route mesh channel: "
                    + registrySpotRemoteAddresses.routerChannelId());
        }
    }

    private void closeOwnedHandlerExecutor() {
        if (closeHandlerExecutor && handlerExecutor instanceof AutoCloseable closeable) {
            try {
                closeable.close();
            } catch (Exception ex) {
                throw new ZLinkConfigurationException("failed to close handler executor", ex);
            }
        }
    }
}
