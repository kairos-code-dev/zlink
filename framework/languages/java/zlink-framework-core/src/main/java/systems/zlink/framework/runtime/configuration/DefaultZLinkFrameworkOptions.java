package systems.zlink.framework.runtime.configuration;

import java.time.Duration;
import java.util.HashSet;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.Executor;
import systems.zlink.framework.ZLinkHandlerFilter;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.configuration.ClientServerChannelBuilder;
import systems.zlink.framework.configuration.DealerMeshChannelBuilder;
import systems.zlink.framework.configuration.FanoutChannelBuilder;
import systems.zlink.framework.configuration.RouteMeshChannelBuilder;
import systems.zlink.framework.configuration.ZLinkCodecRegistryBuilder;
import systems.zlink.framework.configuration.ZLinkDispatchOptions;
import systems.zlink.framework.configuration.ZLinkDiscoveryBuilder;
import systems.zlink.framework.configuration.ZLinkFrameworkOptions;
import systems.zlink.framework.configuration.ZLinkMetadataPolicyBuilder;
import systems.zlink.framework.configuration.ZLinkRegistrySpotRemoteAddressesOptions;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.configuration.ZLinkStreamNodeBuilder;
import systems.zlink.framework.configuration.ZLinkWorkerOptions;
import systems.zlink.framework.errors.ZLinkConfigurationException;
import systems.zlink.framework.runtime.channels.ChannelBuilders;
import systems.zlink.framework.runtime.channels.ChannelKind;
import systems.zlink.framework.runtime.channels.ChannelRegistration;
import systems.zlink.framework.runtime.handlers.ZLinkSuspendHandlerInvoker;
import systems.zlink.framework.runtime.spots.SpotBuilders;
import systems.zlink.framework.runtime.spots.SpotNodeRegistration;
import systems.zlink.framework.runtime.streams.StreamBuilders;
import systems.zlink.framework.runtime.streams.StreamNodeRegistration;
import systems.zlink.framework.spots.ZLinkSpotRemoteAddressResolver;

public final class DefaultZLinkFrameworkOptions implements ZLinkFrameworkOptions {
    private final ZLinkFrameworkRegistration registration = new ZLinkFrameworkRegistration();
    private final Set<String> channelNames = new HashSet<>();
    private final Set<String> spotMeshNames = new HashSet<>();
    private final Set<Class<?>> spotFactoryTypes = new HashSet<>();
    private final Set<String> streamNodeNames = new HashSet<>();
    @Override
    public Duration defaultRequestTimeout() {
        return registration.defaultRequestTimeout();
    }

    @Override
    public void setDefaultRequestTimeout(Duration timeout) {
        registration.setDefaultRequestTimeout(requirePositive(timeout, "timeout"));
    }

    @Override
    public ZLinkCodecRegistryBuilder codecs() {
        return registration.codecs();
    }

    @Override
    public void addHandlersFromPackageOf(Class<?> markerType) {
        registration.handlerPackageMarkers().add(Objects.requireNonNull(markerType, "markerType"));
    }

    @Override
    public ZLinkMetadataPolicyBuilder configureMetadata() {
        return registration.metadataPolicy();
    }

    @Override
    public ZLinkDiscoveryBuilder useDiscovery() {
        return endpoint -> registration.registryEndpoints().add(requireName(endpoint, "registry endpoint"));
    }

    public ClientServerChannelBuilder addClientServerChannel(String channelName)
    {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.CLIENT_SERVER);
        registration.channels().add(channel);
        return ChannelBuilders.clientServer(channel);
    }

    @Override
    public FanoutChannelBuilder addFanoutChannel(String channelName)
    {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.FANOUT);
        registration.channels().add(channel);
        return ChannelBuilders.fanout(channel);
    }

    @Override
    public DealerMeshChannelBuilder addDealerMeshChannel(String channelName)
    {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.DEALER_MESH);
        registration.channels().add(channel);
        return ChannelBuilders.dealerMesh(channel);
    }

    @Override
    public RouteMeshChannelBuilder addRouteMeshChannel(String channelName)
    {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.ROUTE_MESH);
        registration.channels().add(channel);
        return ChannelBuilders.routeMesh(channel);
    }

    @Override
    public ZLinkSpotMeshBuilder addSpotMesh(String channelName)
    {
        String meshName = requireName(channelName, "spot mesh");
        addUnique(spotMeshNames, meshName, "spot mesh");
        return SpotBuilders.mesh(meshName, registration, this::addSpotFactoryType);
    }

    @Override
    public ZLinkStreamNodeBuilder addStreamNode(String streamNodeName)
    {
        String name = requireName(streamNodeName, "stream node");
        addUnique(streamNodeNames, name, "stream node");
        StreamNodeRegistration streamNode = new StreamNodeRegistration(name);
        registration.streamNodes().add(streamNode);
        return StreamBuilders.streamNode(streamNode);
    }

    @Override
    public void addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType) {
        Objects.requireNonNull(factoryType, "factoryType");
        String type = requireName(actorType, "actor type");
        if (registration.actorFactories().putIfAbsent(type, factoryType) != null) {
            throw new ZLinkConfigurationException("duplicate actor type: " + type);
        }
    }

    @Override
    public void addSpotRemoteAddressResolver(
        Class<? extends ZLinkSpotRemoteAddressResolver> resolverType) {
        registration.setSpotRemoteAddressResolverType(
            Objects.requireNonNull(resolverType, "resolverType"));
    }

    @Override
    public ZLinkRegistrySpotRemoteAddressesOptions useRegistrySpotRemoteAddresses(String namespaceName) {
        registration.setRegistrySpotRemoteAddresses(
            new ZLinkRegistrySpotRemoteAddressesRegistration(
                requireName(namespaceName, "namespaceName")));
        return registration.registrySpotRemoteAddresses();
    }

    @Override
    public void useFilter(Class<? extends ZLinkHandlerFilter> filterType) {
        Class<? extends ZLinkHandlerFilter> type =
            Objects.requireNonNull(filterType, "filterType");
        if (!registration.filters().contains(type)) {
            registration.filters().add(type);
        }
    }

    @Override
    public ZLinkDispatchOptions configureDispatch() {
        return registration.dispatchOptions();
    }

    @Override
    public ZLinkWorkerOptions configureWorkers() {
        return registration.workers();
    }

    @Override
    public void useVirtualThreadHandlers() {
        registration.useVirtualThreadHandlers();
    }

    @Override
    public void useHandlerExecutor(Executor executor) {
        registration.useHandlerExecutor(executor);
    }

    @Override
    public void useSuspendHandlerInvoker(ZLinkSuspendHandlerInvoker invoker) {
        registration.useSuspendHandlerInvoker(invoker);
    }

    private void addChannel(String channelName) {
        addUnique(channelNames, channelName, "channel");
    }

    private void addSpotFactoryType(Class<?> spotFactory) {
        if (!spotFactoryTypes.add(spotFactory)) {
            throw new ZLinkConfigurationException(
                "duplicate spot factory type: " + spotFactory.getName());
        }
    }

    public void validate() {
        registration.validate();
    }

    public ZLinkFrameworkRegistration registration() {
        return registration;
    }

    private static void addUnique(Set<String> names, String value, String label) {
        String name = requireName(value, label);
        if (!names.add(name)) {
            throw new ZLinkConfigurationException("duplicate " + label + " name: " + name);
        }
    }

    private static String requireName(String value, String label) {
        if (value == null || value.isBlank()) {
            throw new ZLinkConfigurationException(label + " name is required");
        }
        return value;
    }

    private static Duration requirePositive(Duration value, String label) {
        Objects.requireNonNull(value, label);
        if (value.isNegative() || value.isZero()) {
            throw new ZLinkConfigurationException(label + " must be positive");
        }
        return value;
    }
}
