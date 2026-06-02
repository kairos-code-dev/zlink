package systems.zlink.framework.configuration;

import java.util.function.Consumer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkSpotNodeBuilder {
    void enableRouter();

    void enableRouter(Consumer<SpotRouterCapabilityBuilder> configure);

    void enablePubSub();

    void enablePubSub(Consumer<SpotPubSubCapabilityBuilder> configure);

    void attachChannelClient(String channelName);

    void attachChannelClient(
        String channelName,
        Consumer<SpotChannelClientCapabilityBuilder> configure);

    void attachSpotPublisherClient(String channelName);

    void attachSpotPublisherClient(
        String channelName,
        Consumer<SpotPublisherClientCapabilityBuilder> configure);

    void acceptSpotRoutesFromChannel(String channelName);

    void acceptSpotRoutesFromChannel(
        String channelName,
        Consumer<ZLinkSpotRouteChannelAcceptanceBuilder> configure);

    void configureEntrySpot(Consumer<ZLinkEntrySpotOptions> configure);

    void addSpotFactory(Class<? extends ZLinkSpot> spotType);

    void addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType);
}
