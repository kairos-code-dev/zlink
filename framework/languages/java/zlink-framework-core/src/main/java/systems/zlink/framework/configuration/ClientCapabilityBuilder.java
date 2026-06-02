package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface ClientCapabilityBuilder {
    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
