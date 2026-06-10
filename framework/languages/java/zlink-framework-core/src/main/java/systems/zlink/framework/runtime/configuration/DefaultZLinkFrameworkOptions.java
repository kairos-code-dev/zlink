package systems.zlink.framework.runtime.configuration;

import java.time.Duration;
import java.util.HashSet;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.Executor;
import java.util.function.Consumer;
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
    private Duration defaultTimeout = Duration.ofSeconds(30);

    @Override
    public Duration defaultTimeout() {
        return registration.defaultTimeout();
    }

    @Override
    public void setDefaultTimeout(Duration timeout) {
        defaultTimeout = requirePositive(timeout, "timeout");
        registration.setDefaultTimeout(defaultTimeout);
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
    public void configureMetadata(Consumer<ZLinkMetadataPolicyBuilder> configure) {
        configure.accept(registration.metadataPolicy());
    }

    @Override
    public void useDiscovery(Consumer<ZLinkDiscoveryBuilder> configure) {
        configure.accept(endpoint -> registration.registryEndpoints().add(requireName(endpoint, "registry endpoint")));
    }

    public void addClientServerChannel(
        String channelName,
        Consumer<ClientServerChannelBuilder> configure) {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.CLIENT_SERVER);
        registration.channels().add(channel);
        configure.accept(ChannelBuilders.clientServer(channel));
    }

    @Override
    public void addFanoutChannel(
        String channelName,
        Consumer<FanoutChannelBuilder> configure) {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.FANOUT);
        registration.channels().add(channel);
        configure.accept(ChannelBuilders.fanout(channel));
    }

    @Override
    public void addDealerMeshChannel(
        String channelName,
        Consumer<DealerMeshChannelBuilder> configure) {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.DEALER_MESH);
        registration.channels().add(channel);
        configure.accept(ChannelBuilders.dealerMesh(channel));
    }

    @Override
    public void addRouteMeshChannel(
        String channelName,
        Consumer<RouteMeshChannelBuilder> configure) {
        addChannel(channelName);
        ChannelRegistration channel = new ChannelRegistration(channelName, ChannelKind.ROUTE_MESH);
        registration.channels().add(channel);
        configure.accept(ChannelBuilders.routeMesh(channel));
    }

    @Override
    public void addSpotMesh(
        String channelName,
        Consumer<ZLinkSpotMeshBuilder> configure) {
        String meshName = requireName(channelName, "spot mesh");
        addUnique(spotMeshNames, meshName, "spot mesh");
        int before = registration.spotNodes().size();
        configure.accept(SpotBuilders.mesh(meshName, registration));
        for (SpotNodeRegistration node : registration.spotNodes().subList(
            before,
            registration.spotNodes().size())) {
            for (Class<?> spotFactory : node.spotFactories()) {
                if (!spotFactoryTypes.add(spotFactory)) {
                    throw new ZLinkConfigurationException(
                        "duplicate spot factory type: " + spotFactory.getName());
                }
            }
        }
    }

    @Override
    public void addStreamNode(
        String streamNodeName,
        Consumer<ZLinkStreamNodeBuilder> configure) {
        String name = requireName(streamNodeName, "stream node");
        addUnique(streamNodeNames, name, "stream node");
        StreamNodeRegistration streamNode = new StreamNodeRegistration(name);
        registration.streamNodes().add(streamNode);
        configure.accept(StreamBuilders.streamNode(streamNode));
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
    public void useRegistrySpotRemoteAddresses(String namespaceName) {
        registration.setRegistrySpotRemoteAddresses(
            new ZLinkRegistrySpotRemoteAddressesRegistration(
                requireName(namespaceName, "namespaceName")));
    }

    @Override
    public void useRegistrySpotRemoteAddresses(
        String namespaceName,
        Consumer<ZLinkRegistrySpotRemoteAddressesOptions> configure) {
        useRegistrySpotRemoteAddresses(namespaceName);
        configure.accept(registration.registrySpotRemoteAddresses());
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
    public void configureDispatch(Consumer<ZLinkDispatchOptions> configure) {
        configure.accept(registration.dispatchOptions());
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
