package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface ZLinkSpotRouteChannelAcceptanceBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
