package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface SpotChannelClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
