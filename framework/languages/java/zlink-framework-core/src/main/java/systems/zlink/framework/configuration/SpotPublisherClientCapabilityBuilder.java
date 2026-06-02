package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface SpotPublisherClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
