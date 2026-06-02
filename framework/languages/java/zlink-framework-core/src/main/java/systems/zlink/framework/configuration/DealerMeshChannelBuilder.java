package systems.zlink.framework.configuration;

import java.util.function.Consumer;

public interface DealerMeshChannelBuilder {
    void enableClient();

    void enableClient(Consumer<ClientCapabilityBuilder> configure);

    void addHandlerGroup(String groupName);
}
