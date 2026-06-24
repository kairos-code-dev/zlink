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
        SpotNodeRegistration node,
        ZLinkFrameworkRegistration registration,
        Consumer<Class<?>> spotFactoryAdded) {
        return new Mesh(meshName, node, registration, spotFactoryAdded);
    }

    private record Mesh(
        String meshName,
        SpotNodeRegistration node,
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

        public ZLinkSpotNodeBuilder enableRouter(String endpoint) {
            node.enableRouter();
            node.setRouterBind(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder connectRouter(String endpoint) {
            node.addRouterManualConnection(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder connectRouter(RoutingId peerRoutingId, String endpoint) {
            node.addRouterManualConnection(peerRoutingId, endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder setRouterRoutingId(RoutingId routingId) {
            node.enableRouter();
            node.setRouterRoutingId(routingId);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder enablePubSub(String endpoint) {
            node.enablePubSub();
            node.setPubBind(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder connectPeerPub(String endpoint) {
            node.addPubSubManualConnection(endpoint);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder setPubSubRoutingId(RoutingId routingId) {
            node.enablePubSub();
            node.setPubSubRoutingId(routingId);
            return this;
        }

        @Override
        public ZLinkEntrySpotOptions configureEntrySpot() {
            return node.entrySpotOptions();
        }

        @Override
        public ZLinkSpotNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType) {
            node.addSpotFactory(spotType);
            spotFactoryAdded.accept(spotType);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType) {
            node.addEntrySpot(entrySpotType);
            return this;
        }
    }

}
