package systems.zlink.framework.runtime.spots;

import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ZLinkEntrySpotOptions;
import systems.zlink.framework.configuration.ZLinkDiscoveryBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.runtime.configuration.ZLinkRegistrySpotRemoteAddressesRegistration;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public final class SpotBuilders {
    private SpotBuilders() {
    }

    public static ZLinkSpotMeshBuilder mesh(
        String meshName,
        ZLinkFrameworkRegistration registration,
        Consumer<Class<?>> spotFactoryAdded) {
        return new Mesh(meshName, registration, spotFactoryAdded);
    }

    private record Mesh(
        String meshName,
        ZLinkFrameworkRegistration registration,
        Consumer<Class<?>> spotFactoryAdded) implements ZLinkSpotMeshBuilder {
        @Override
        public ZLinkDiscoveryBuilder useDiscovery() {
            return registration.registryEndpoints()::add;
        }

        @Override
        public ZLinkSpotMeshBuilder useRegistrySpotResolver() {
            if (registration.spotRemoteAddressResolverType() != null
                || registration.registrySpotRemoteAddresses() != null) {
                throw new systems.zlink.framework.errors.ZLinkConfigurationException(
                    "SPOT remote address resolver is already registered.");
            }
            registration.setRegistrySpotRemoteAddresses(
                new ZLinkRegistrySpotRemoteAddressesRegistration(meshName));
            return this;
        }

        public ZLinkSpotNodeBuilder addNode(String spotNodeName) {
            SpotNodeRegistration node = new SpotNodeRegistration(meshName, spotNodeName);
            registration.spotNodes().add(node);
            return new Node(node, spotFactoryAdded);
        }
    }

    private record Node(
        SpotNodeRegistration registration,
        Consumer<Class<?>> spotFactoryAdded) implements ZLinkSpotNodeBuilder {
        public ZLinkSpotNodeBuilder enableRouter(String endpoint) {
            registration.enableRouter();
            registration.setRouterBind(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder connectRouter(String endpoint) {
            registration.addRouterManualConnection(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder connectRouter(RoutingId peerRoutingId, String endpoint) {
            registration.addRouterManualConnection(peerRoutingId, endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder setRouterRoutingId(RoutingId routingId) {
            registration.enableRouter();
            registration.setRouterRoutingId(routingId);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder enablePubSub(String endpoint) {
            registration.enablePubSub();
            registration.setPubBind(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder connectPeerPub(String endpoint) {
            registration.addPubSubManualConnection(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder setPubSubRoutingId(RoutingId routingId) {
            registration.enablePubSub();
            registration.setPubSubRoutingId(routingId);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder attachSpotPublisherClient(String channelName) {
            registration.attachSpotPublisherClient(channelName);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder attachSpotPublisherClient(String channelName, String endpoint) {
            registration.attachSpotPublisherClient(channelName).addManualConnection(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder acceptSpotRoutesFromChannel(String channelName) {
            registration.acceptSpotRoutesFromChannel(channelName);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder acceptSpotRoutesFromChannel(String channelName, String endpoint) {
            registration.acceptSpotRoutesFromChannel(channelName).addManualConnection(endpoint);
            return this;
        }

        @Override
        public ZLinkEntrySpotOptions configureEntrySpot() {
            return registration.entrySpotOptions();
        }

        @Override
        public ZLinkSpotNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType) {
            registration.addSpotFactory(spotType);
            spotFactoryAdded.accept(spotType);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
            registration.addEntrySpot(entrySpotType);
            return this;
        }
    }

}
