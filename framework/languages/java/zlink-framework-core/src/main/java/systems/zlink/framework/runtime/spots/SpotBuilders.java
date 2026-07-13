package systems.zlink.framework.runtime.spots;

import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.configuration.ZLinkEntrySpotOptions;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.monitoring.ZLinkSpotDrainPolicy;

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
        public ZLinkSpotMeshBuilder useDrainPolicy(ZLinkSpotDrainPolicy policy) {
            node.setDrainPolicy(policy);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder setRoutingId(RoutingId routingId) {
            node.setRoutingId(routingId);
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

        @Override
        public ZLinkSpotNodeBuilder addActorFactory(
            String actorType,
            Class<? extends ZLinkActorFactory> factoryType) {
            node.addActorFactory(actorType, factoryType);
            return this;
        }

        @Override
        public ZLinkSpotNodeBuilder addActorTransferAdapter(
            String actorType,
            Class<? extends ZLinkActorTransferAdapter<?>> adapterType) {
            node.addActorTransferAdapter(actorType, adapterType);
            return this;
        }
    }

}
