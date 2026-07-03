package systems.zlink.framework.runtime.configuration;

import java.time.Duration;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;
import systems.zlink.framework.runtime.channels.ChannelRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkHandlerScanner;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandler;
import systems.zlink.framework.runtime.handlers.ZLinkScannedHandlerCatalog;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.locations.ZLinkLocationRegistration;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;
import systems.zlink.framework.runtime.streams.StreamNodeRegistration;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;
import systems.zlink.framework.streams.ZLinkStreamCompressionCodec;
import systems.zlink.framework.streams.ZLinkStreamCompressionCodecs;

public final class ZLinkFrameworkRegistration {
    private final ZLinkCodecRegistration codecs = new ZLinkCodecRegistration();
    private final ZLinkMetadataPolicyRegistration metadataPolicy =
        new ZLinkMetadataPolicyRegistration();
    private final ZLinkDispatchOptionsRegistration dispatchOptions =
        new ZLinkDispatchOptionsRegistration();
    private final ZLinkWorkerOptionsRegistration workers =
        new ZLinkWorkerOptionsRegistration();
    private final ZLinkLocationRegistration locations = new ZLinkLocationRegistration();
    private final List<ChannelRegistration> channels = new ArrayList<>();
    private final List<SpotNodeRegistration> spotNodes = new ArrayList<>();
    private final List<StreamNodeRegistration> streamNodes = new ArrayList<>();
    private final Set<Class<?>> handlerPackageMarkers = new LinkedHashSet<>();
    private final List<Class<? extends ZLinkHandlerFilter>> filters = new ArrayList<>();
    private final List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers = new ArrayList<>();
    private ZLinkStreamCompressionCodec streamCompressionCodec = ZLinkStreamCompressionCodecs.lz4();
    private Executor handlerExecutor = Executors.newVirtualThreadPerTaskExecutor();
    private boolean closeHandlerExecutor = true;
    private Duration defaultRequestTimeout = Duration.ofSeconds(30);
    private Class<? extends ZLinkSpotRemoteAddressResolver> spotRemoteAddressResolverType;

    public Duration defaultRequestTimeout() {
        return defaultRequestTimeout;
    }

    void setDefaultRequestTimeout(Duration defaultRequestTimeout) {
        this.defaultRequestTimeout = defaultRequestTimeout;
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

    public ZLinkWorkerOptionsRegistration workers() {
        return workers;
    }

    public ZLinkLocationRegistration locations() {
        return locations;
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

    public Set<Class<?>> handlerPackageMarkers() {
        return handlerPackageMarkers;
    }

    public List<Class<? extends ZLinkHandlerFilter>> filters() {
        return filters;
    }

    public List<ZLinkSuspendHandlerInvoker> suspendHandlerInvokers() {
        return List.copyOf(suspendHandlerInvokers);
    }

    public ZLinkStreamCompressionCodec streamCompressionCodec() {
        return streamCompressionCodec;
    }

    public void useStreamCompression(ZLinkStreamCompressionCodec codec) {
        streamCompressionCodec = codec;
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
        if (spotRemoteAddressResolverType != null) {
            types.add(spotRemoteAddressResolverType);
        }
        if (locations.peerStoreType() != null) {
            types.add(locations.peerStoreType());
        }
        if (locations.spotStoreType() != null) {
            types.add(locations.spotStoreType());
        }
        if (locations.actorStoreType() != null) {
            types.add(locations.actorStoreType());
        }
        if (locations.routeStoreType() != null) {
            types.add(locations.routeStoreType());
        }
        if (locations.ownerLeaseStoreType() != null) {
            types.add(locations.ownerLeaseStoreType());
        }
        for (ChannelRegistration channel : channels) {
            types.addAll(channel.handlerTypes());
        }
        for (SpotNodeRegistration spotNode : spotNodes) {
            types.addAll(spotNode.spotFactories());
            types.addAll(spotNode.entrySpots());
            types.addAll(spotNode.actorFactories().values());
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

    void setPeerLocationStoreType(Class<? extends ZLinkPeerLocationStore> storeType) {
        locations.setPeerStoreType(storeType);
    }

    void setSpotLocationStoreType(Class<? extends ZLinkSpotLocationStore> storeType) {
        locations.setSpotStoreType(storeType);
    }

    void setActorLocationStoreType(Class<? extends ZLinkActorLocationStore> storeType) {
        locations.setActorStoreType(storeType);
    }

    void setRouteLocationStoreType(Class<? extends ZLinkRouteLocationStore> storeType) {
        locations.setRouteStoreType(storeType);
    }

    void setOwnerLeaseStoreType(Class<? extends ZLinkOwnerLeaseStore> storeType) {
        locations.setOwnerLeaseStoreType(storeType);
    }

    void useInMemoryLocationStores() {
        locations.enableInMemoryStores();
    }

    void setLocationStore(ZLinkLocationStore store) {
        locations.setStoreInstance(store);
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

    void validate() {
        dispatchOptions.validate();
        workers.validate();
        validateLocations();
        ZLinkScannedHandlerCatalog handlerCatalog =
            ZLinkHandlerScanner.scan(handlerPackageMarkers);
        for (ChannelRegistration channel : channels) {
            channel.validate(locations.enabled(), handlerCatalog);
        }
        int actorCapableNodes = 0;
        for (SpotNodeRegistration spotNode : spotNodes) {
            spotNode.validate();
            if (!spotNode.actorFactories().isEmpty()) {
                actorCapableNodes++;
            }
        }
        if (actorCapableNodes > 1) {
            throw new ZLinkConfigurationException(
                "actor factory registration is ambiguous because more than one SpotNode owns actor factories");
        }
        for (StreamNodeRegistration streamNode : streamNodes) {
            streamNode.validate(spotNodes);
        }
    }

    private void validateLocations() {
        if (locations.useInMemoryStores() && locations.hasAnyStoreType()) {
            throw new ZLinkConfigurationException(
                "in-memory location stores cannot be combined with explicit location store registrations");
        }
        if (locations.storeInstance() != null
            && (locations.useInMemoryStores() || locations.hasAnyStoreType())) {
            throw new ZLinkConfigurationException(
                "addLocationStore registers every store role at once and cannot be combined with "
                    + "useInMemoryLocationStores or per-role add*LocationStore registrations");
        }
        if (locations.hasAnyStoreType() && !locations.hasAllStoreTypes()) {
            throw new ZLinkConfigurationException(
                "location stores are all-or-nothing: register peer, spot, actor, route, and owner lease stores together");
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
