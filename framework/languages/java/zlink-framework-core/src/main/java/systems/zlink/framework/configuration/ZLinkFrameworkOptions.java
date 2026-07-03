package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.concurrent.Executor;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.locations.ZLinkActorLocationStore;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.locations.ZLinkOwnerLeaseStore;
import systems.zlink.framework.locations.ZLinkPeerLocationStore;
import systems.zlink.framework.locations.ZLinkRouteLocationStore;
import systems.zlink.framework.locations.ZLinkSpotLocationStore;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public interface ZLinkFrameworkOptions {
    Duration defaultRequestTimeout();

    void setDefaultRequestTimeout(Duration timeout);

    ZLinkCodecRegistryBuilder codecs();

    void addHandlersFromPackageOf(Class<?> markerType);

    ZLinkMetadataPolicyBuilder configureMetadata();

    ClientServerChannelBuilder addClientServerChannel(String channelName);

    FanoutChannelBuilder addFanoutChannel(String channelName);

    RouteMeshChannelBuilder addRouteMesh(String channelName);

    ZLinkSpotMeshBuilder addSpotMesh(String channelName);

    ZLinkStreamNodeBuilder addStreamNode(String streamNodeName);

    void addSpotRemoteAddressResolver(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType);

    void addPeerLocationStore(Class<? extends ZLinkPeerLocationStore> storeType);

    void addSpotLocationStore(Class<? extends ZLinkSpotLocationStore> storeType);

    void addActorLocationStore(Class<? extends ZLinkActorLocationStore> storeType);

    void addRouteLocationStore(Class<? extends ZLinkRouteLocationStore> storeType);

    void addOwnerLeaseStore(Class<? extends ZLinkOwnerLeaseStore> storeType);

    void useInMemoryLocationStores();

    void addLocationStore(ZLinkLocationStore store);

    ZLinkLocationOptions configureLocations();

    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);

    ZLinkDispatchOptions configureDispatch();

    ZLinkStreamCompressionBuilder configureStreamCompression();

    ZLinkWorkerOptions configureWorkers();

    void useVirtualThreadHandlers();

    void useHandlerExecutor(Executor executor);

    void useSuspendHandlerInvoker(ZLinkSuspendHandlerInvoker invoker);
}
