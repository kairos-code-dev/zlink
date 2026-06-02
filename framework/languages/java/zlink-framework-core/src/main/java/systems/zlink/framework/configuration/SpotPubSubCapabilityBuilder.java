package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface SpotPubSubCapabilityBuilder {
    void setPubBind(String endpoint);

    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
