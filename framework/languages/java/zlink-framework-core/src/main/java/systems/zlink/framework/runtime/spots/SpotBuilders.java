package systems.zlink.framework.runtime.spots;

import java.util.function.Consumer;
import systems.zlink.framework.configuration.RegistryBuilder;
import systems.zlink.framework.configuration.SpotPubSubCapabilityBuilder;
import systems.zlink.framework.configuration.SpotRouterCapabilityBuilder;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;

public final class SpotBuilders {
    private SpotBuilders() {
    }

    public static ZLinkSpotMeshBuilder mesh(
        String meshName,
        ZLinkFrameworkRegistration registration) {
        return new Mesh(meshName, registration);
    }

    private record Mesh(
        String meshName,
        ZLinkFrameworkRegistration registration) implements ZLinkSpotMeshBuilder {
        @Override
        public void useDiscovery(Consumer<RegistryBuilder> configure) {
            configure.accept(endpoint ->
                registration.registryEndpoints().add(endpoint));
        }

        @Override
        public void addNode(String spotNodeName, Consumer<ZLinkSpotNodeBuilder> configure) {
            SpotNodeRegistration node = new SpotNodeRegistration(meshName, spotNodeName);
            registration.spotNodes().add(node);
            configure.accept(new Node(node));
        }
    }

    private record Node(SpotNodeRegistration registration) implements ZLinkSpotNodeBuilder {
        @Override
        public void enableRouter() {
        }

        @Override
        public void enableRouter(Consumer<SpotRouterCapabilityBuilder> configure) {
            configure.accept(new Router(registration));
        }

        @Override
        public void enablePubSub() {
        }

        @Override
        public void enablePubSub(Consumer<SpotPubSubCapabilityBuilder> configure) {
            configure.accept(new PubSub(registration));
        }

        @Override
        public void addSpotFactory(Class<? extends systems.zlink.framework.spots.ZLinkSpot> spotType) {
            registration.addSpotFactory(spotType);
        }

        @Override
        public void addEntrySpot(Class<? extends systems.zlink.framework.spots.ZLinkEntrySpot> entrySpotType) {
            registration.addEntrySpot(entrySpotType);
        }
    }

    private record Router(SpotNodeRegistration registration) implements SpotRouterCapabilityBuilder {
        @Override
        public void setRouterBind(String endpoint) {
            registration.setRouterBind(endpoint);
        }

        @Override
        public void useManualConnections(Consumer<systems.zlink.framework.configuration.ManualEndpointListBuilder> configure) {
        }
    }

    private record PubSub(SpotNodeRegistration registration) implements SpotPubSubCapabilityBuilder {
        @Override
        public void setPubBind(String endpoint) {
            registration.setPubBind(endpoint);
        }

        @Override
        public void useManualConnections(Consumer<systems.zlink.framework.configuration.ManualEndpointListBuilder> configure) {
        }
    }
}
