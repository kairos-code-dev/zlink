package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface SpotRouterCapabilityBuilder {
    void setRouterBind(String endpoint);

    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
