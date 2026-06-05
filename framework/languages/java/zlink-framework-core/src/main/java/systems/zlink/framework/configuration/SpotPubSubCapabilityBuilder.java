package systems.zlink.framework.configuration;

import java.util.function.Consumer;
import systems.zlink.contracts.core.RoutingId;

public interface SpotPubSubCapabilityBuilder {
    void bindPubSub(String endpoint);

    void setRoutingId(RoutingId routingId);

    void useManualConnections(Consumer<ManualEndpointListBuilder> configure);
}
