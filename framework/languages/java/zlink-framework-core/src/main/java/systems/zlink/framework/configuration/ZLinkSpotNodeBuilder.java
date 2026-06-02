package systems.zlink.framework.configuration;

import java.util.function.Consumer;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkSpotNodeBuilder {
    void enableRouter();

    void enableRouter(Consumer<SpotRouterCapabilityBuilder> configure);

    void enablePubSub();

    void enablePubSub(Consumer<SpotPubSubCapabilityBuilder> configure);

    void addSpotFactory(Class<? extends ZLinkSpot> spotType);

    void addEntrySpot(Class<? extends ZLinkEntrySpot> entrySpotType);
}
