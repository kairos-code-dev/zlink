package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.concurrent.Executor;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public interface ZLinkFrameworkOptions {
    Duration defaultRequestTimeout();

    void setDefaultRequestTimeout(Duration timeout);

    ZLinkCodecRegistryBuilder codecs();

    void addHandlersFromPackageOf(Class<?> markerType);

    ZLinkMetadataPolicyBuilder configureMetadata();

    ZLinkDiscoveryBuilder useDiscovery();

    ClientServerChannelBuilder addClientServerChannel(String channelName);

    FanoutChannelBuilder addFanoutChannel(String channelName);

    DealerMeshChannelBuilder addDealerMeshChannel(String channelName);

    RouteMeshChannelBuilder addRouteMeshChannel(String channelName);

    ZLinkSpotMeshBuilder addSpotMesh(String channelName);

    ZLinkStreamNodeBuilder addStreamNode(String streamNodeName);

    void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);

    void addSpotRemoteAddressResolver(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType);

    ZLinkRegistrySpotRemoteAddressesOptions useRegistrySpotRemoteAddresses(String namespaceName);

    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);

    ZLinkDispatchOptions configureDispatch();

    ZLinkWorkerOptions configureWorkers();

    void useVirtualThreadHandlers();

    void useHandlerExecutor(Executor executor);

    void useSuspendHandlerInvoker(ZLinkSuspendHandlerInvoker invoker);
}
