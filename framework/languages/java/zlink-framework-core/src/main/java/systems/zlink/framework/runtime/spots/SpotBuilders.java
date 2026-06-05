package systems.zlink.framework.runtime.spots;

import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.configuration.ManualEndpointListBuilder;
import systems.zlink.framework.configuration.RegistryBuilder;
import systems.zlink.framework.configuration.SpotChannelClientCapabilityBuilder;
import systems.zlink.framework.configuration.SpotPubSubCapabilityBuilder;
import systems.zlink.framework.configuration.SpotPublisherClientCapabilityBuilder;
import systems.zlink.framework.configuration.SpotRouterCapabilityBuilder;
import systems.zlink.framework.configuration.ZLinkEntrySpotOptions;
import systems.zlink.framework.configuration.ZLinkSpotMeshBuilder;
import systems.zlink.framework.configuration.ZLinkSpotNodeBuilder;
import systems.zlink.framework.configuration.ZLinkSpotRouteChannelAcceptanceBuilder;
import systems.zlink.framework.runtime.configuration.ZLinkFrameworkRegistration;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

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
            registration.enableRouter();
        }

        @Override
        public void enableRouter(Consumer<SpotRouterCapabilityBuilder> configure) {
            registration.enableRouter();
            configure.accept(new Router(registration));
        }

        @Override
        public void enablePubSub() {
            registration.enablePubSub();
        }

        @Override
        public void enablePubSub(Consumer<SpotPubSubCapabilityBuilder> configure) {
            registration.enablePubSub();
            configure.accept(new PubSub(registration));
        }

        @Override
        public void attachChannelClient(String channelName) {
            registration.attachChannelClient(channelName);
        }

        @Override
        public void attachChannelClient(
            String channelName,
            Consumer<SpotChannelClientCapabilityBuilder> configure) {
            configure.accept(new ChannelClient(registration.attachChannelClient(channelName)));
        }

        @Override
        public void attachSpotPublisherClient(String channelName) {
            registration.attachSpotPublisherClient(channelName);
        }

        @Override
        public void attachSpotPublisherClient(
            String channelName,
            Consumer<SpotPublisherClientCapabilityBuilder> configure) {
            configure.accept(new Publisher(registration.attachSpotPublisherClient(channelName)));
        }

        @Override
        public void acceptSpotRoutesFromChannel(String channelName) {
            registration.acceptSpotRoutesFromChannel(channelName);
        }

        @Override
        public void acceptSpotRoutesFromChannel(
            String channelName,
            Consumer<ZLinkSpotRouteChannelAcceptanceBuilder> configure) {
            configure.accept(new Acceptance(
                registration.acceptSpotRoutesFromChannel(channelName)));
        }

        @Override
        public void configureEntrySpot(Consumer<ZLinkEntrySpotOptions> configure) {
            configure.accept(registration.entrySpotOptions());
        }

        @Override
        public void addSpotFactory(Class<? extends ZLinkSpot> spotType) {
            registration.addSpotFactory(spotType);
        }

        @Override
        public void addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType) {
            registration.addEntrySpot(entrySpotType);
        }
    }

    private record Router(SpotNodeRegistration registration) implements SpotRouterCapabilityBuilder {
        @Override
        public void setRouterBind(String endpoint) {
            registration.setRouterBind(endpoint);
        }

        @Override
        public void setRoutingId(RoutingId routingId) {
            registration.enableRouter();
            registration.setRouterRoutingId(routingId);
        }

        @Override
        public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(registration::addRouterManualConnection);
        }
    }

    private record PubSub(SpotNodeRegistration registration) implements SpotPubSubCapabilityBuilder {
        @Override
        public void setPubBind(String endpoint) {
            registration.setPubBind(endpoint);
        }

        @Override
        public void setRoutingId(RoutingId routingId) {
            registration.enablePubSub();
            registration.setPubSubRoutingId(routingId);
        }

        @Override
        public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(registration::addPubSubManualConnection);
        }
    }

    private record Publisher(
        SpotPublisherClientRegistration registration) implements SpotPublisherClientCapabilityBuilder {
        @Override
        public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(registration::addManualConnection);
        }
    }

    private record ChannelClient(
        SpotChannelClientRegistration registration) implements SpotChannelClientCapabilityBuilder {
        @Override
        public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(registration::addManualConnection);
        }
    }

    private record Acceptance(
        SpotRouteChannelAcceptanceRegistration registration)
        implements ZLinkSpotRouteChannelAcceptanceBuilder {
        @Override
        public void useManualConnections(Consumer<ManualEndpointListBuilder> configure) {
            configure.accept(registration::addManualConnection);
        }
    }
}
