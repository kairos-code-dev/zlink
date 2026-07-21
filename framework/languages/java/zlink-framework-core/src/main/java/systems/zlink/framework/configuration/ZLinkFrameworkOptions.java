package systems.zlink.framework.configuration;

import java.time.Duration;
import java.util.concurrent.Executor;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.locations.ZLinkLocationOptions;
import systems.zlink.framework.locations.ZLinkLocationStore;
import systems.zlink.framework.runtime.internal.handlers.ZLinkSuspendInvocationAdapter;

public interface ZLinkFrameworkOptions {
    Duration defaultRequestTimeout();

    void setDefaultRequestTimeout(Duration timeout);

    Duration actorTransferForwardWindow();

    void setActorTransferForwardWindow(Duration window);

    ZLinkCodecRegistryBuilder codecs();

    void addHandlersFromPackageOf(Class<?> markerType);

    ZLinkMetadataPolicyBuilder configureMetadata();

    ZLinkMeshNodeBuilder addRouteMesh(String meshName);

    ClientServerChannelBuilder addClientServerChannel(String channelName);

    FanoutChannelBuilder addFanoutChannel(String channelName);

    RouteMeshChannelBuilder addRouteMeshChannel(String channelName);

    ZLinkSpotMeshBuilder addSpotMesh(String channelName);

    ZLinkStreamNodeBuilder addStreamNode(String streamNodeName);

    void addLocationStore(ZLinkLocationStore store);

    ZLinkLocationOptions configureLocations();

    void useFilter(Class<? extends ZLinkHandlerFilter> filterType);

    ZLinkDispatchOptions configureDispatch();

    ZLinkStreamCompressionBuilder configureStreamCompression();

    ZLinkWorkerOptions configureWorkers();

    void useVirtualThreadHandlers();

    void useHandlerExecutor(Executor executor);

    void useSuspendHandlerInvoker(ZLinkSuspendInvocationAdapter invoker);
}
