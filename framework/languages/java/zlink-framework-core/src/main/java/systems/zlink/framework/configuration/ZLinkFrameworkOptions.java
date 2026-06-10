package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public interface ZLinkFrameworkOptions {
    Duration defaultTimeout();

    void setDefaultTimeout(Duration timeout);

    ZLinkCodecRegistryBuilder codecs();

    void addHandlersFromPackageOf(Class<?> markerType);

    void configureMetadata(Consumer<ZLinkMetadataPolicyBuilder> configure);

    void useDiscovery(Consumer<ZLinkDiscoveryBuilder> configure);

    void addClientServerChannel(
        String channelName,
        Consumer<ClientServerChannelBuilder> configure);

    void addFanoutChannel(
        String channelName,
        Consumer<FanoutChannelBuilder> configure);

    void addDealerMeshChannel(
        String channelName,
        Consumer<DealerMeshChannelBuilder> configure);

    void addRouteMeshChannel(
        String channelName,
        Consumer<RouteMeshChannelBuilder> configure);

    void addSpotMesh(
        String channelName,
        Consumer<ZLinkSpotMeshBuilder> configure);

    void addStreamNode(
        String streamNodeName,
        Consumer<ZLinkStreamNodeBuilder> configure);

    void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);

    void addSpotRemoteAddressResolver(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType);

    void useRegistrySpotRemoteAddresses(String namespaceName);

    void useRegistrySpotRemoteAddresses(
        String namespaceName,
        Consumer<ZLinkRegistrySpotRemoteAddressesOptions> configure);

    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);

    void configureDispatch(Consumer<ZLinkDispatchOptions> configure);

    void useVirtualThreadHandlers();

    void useHandlerExecutor(Executor executor);

    void useSuspendHandlerInvoker(ZLinkSuspendHandlerInvoker invoker);
}
