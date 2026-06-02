package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface SubscriberCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
